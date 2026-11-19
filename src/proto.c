/* Kiosque 32 : telnet et WebSocket, repris de Kiosque 64 (net.c). */
#include "mini.h"
#include "proto.h"

int proto_state;
static int kind;
static proto_sink_fn sink;
static proto_out_fn out;
static unsigned long rnd;

/* --- telnet ---------------------------------------------------------- */
#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
static unsigned char t_state, t_cmd;

static void telnet_byte(unsigned char c)
{
    switch (t_state) {
    case 0:
        if (c == IAC) t_state = 1;
        else sink(c);
        return;
    case 1:                                 /* après IAC */
        if (c == IAC) { t_state = 0; sink(c); return; }
        if (c >= WILL && c <= DONT) { t_cmd = c; t_state = 2; return; }
        t_state = (c == SB) ? 3 : 0;
        return;
    case 2: {                               /* option négociée */
        /* On accepte « binaire » (0), « écho » (1, côté serveur),
           « suppression du go-ahead » (3) et la taille d'écran (31,
           NAWS : 40 x 25) ; on refuse le reste. */
        int ok = (c == 0 || c == 3 || (c == 1 && t_cmd == WILL)
                  || (c == 31 && t_cmd == DO));
        unsigned char r[3];
        if (t_cmd == DO) r[1] = ok ? WILL : WONT;
        else if (t_cmd == WILL) r[1] = ok ? DO : DONT;
        else r[1] = 0;
        if (r[1]) {
            r[0] = IAC; r[2] = c;
            out(r, 3);
        }
        if (c == 31 && t_cmd == DO) {
            static const unsigned char naws[9] = { IAC, SB, 31, 0, 40, 0, 25, IAC, SE };
            out(naws, 9);
        }
        t_state = 0;
        return;
    }
    case 3:                                 /* sous-négociation : ignorée */
        if (c == IAC) t_state = 4;
        return;
    case 4:
        t_state = (c == SE) ? 0 : 3;
        return;
    }
}

/* --- WebSocket ------------------------------------------------------- */
static unsigned char w_state, w_op, w_hdr, w_utf;
static unsigned long w_len;
static unsigned char w_ping[125];
static int w_ping_len;
static unsigned char h_win[4];             /* fin des en-têtes HTTP */
static int h_pos, h_ok;

static void ws_frame(unsigned char op, const unsigned char *p, int n)
{
    unsigned char buf[8 + 512];
    unsigned char key[4];
    int h = 2, i;
    if (n > 512) n = 512;
    buf[0] = 0x80 | op;
    if (n < 126) {
        buf[1] = 0x80 | n;
    } else {
        buf[1] = 0x80 | 126;
        buf[2] = n >> 8;
        buf[3] = n & 0xFF;
        h = 4;
    }
    rnd = rnd * 1103515245UL + 12345UL;    /* clé de masquage du client */
    key[0] = rnd >> 24; key[1] = rnd >> 16; key[2] = rnd >> 8; key[3] = rnd;
    memcpy(buf + h, key, 4);
    h += 4;
    for (i = 0; i < n; ++i)
        buf[h + i] = p[i] ^ key[i & 3];
    out(buf, h + n);
}

static void ws_payload(unsigned char c)
{
    if (w_op == 9) {                        /* ping : on garde pour le pong */
        if (w_ping_len < (int)sizeof(w_ping))
            w_ping[w_ping_len++] = c;
        return;
    }
    if (w_op == 8)
        return;
    /* trames texte en UTF-8 : on ramène les caractères à un octet */
    if (w_utf) {
        sink(((w_utf & 3) << 6) | (c & 0x3F));
        w_utf = 0;
    } else if (c >= 0xC0 && c < 0xE0) {
        w_utf = c;
    } else {
        sink(c);
    }
}

static void ws_end(void)
{
    if (w_op == 9)
        ws_frame(10, w_ping, w_ping_len);
    else if (w_op == 8)
        proto_state = PROTO_CLOSED;
}

static void ws_byte(unsigned char c)
{
    switch (w_state) {
    case 0:                                 /* FIN, opcode */
        if ((c & 0x0F) != 0)                /* 0 = suite de la trame précédente */
            w_op = c & 0x0F;
        if (w_op == 9)
            w_ping_len = 0;
        w_state = 1;
        return;
    case 1:                                 /* longueur */
        c &= 0x7F;                          /* le serveur ne masque pas */
        if (c == 126) { w_hdr = 2; w_len = 0; w_state = 2; return; }
        if (c == 127) { w_hdr = 8; w_len = 0; w_state = 2; return; }
        w_len = c;
        break;
    case 2:                                 /* longueur étendue */
        w_len = (w_len << 8) | c;
        if (--w_hdr)
            return;
        break;
    case 3:                                 /* contenu */
        ws_payload(c);
        if (--w_len)
            return;
        ws_end();
        w_state = 0;
        return;
    }
    if (w_len == 0) {
        ws_end();
        w_state = 0;
    } else {
        w_state = 3;
    }
}

static void put_num(char *q, int n)
{
    char d[8];
    int i = 0;
    do { d[i++] = '0' + n % 10; n /= 10; } while (n);
    q += strlen(q);
    while (i) *q++ = d[--i];
    *q = 0;
}

void proto_ws_open(const char *host, int port, const char *path)
{
    char q[400];
    if (strlen(host) + (path ? strlen(path) : 0) > 300)
        return;
    strcpy(q, "GET ");
    strcat(q, (path && *path) ? path : "/");
    strcat(q, " HTTP/1.1\r\nHost: ");
    strcat(q, host);
    if (port != 80) {
        strcat(q, ":");
        put_num(q, port);
    }
    strcat(q, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              "Sec-WebSocket-Key: S2lvc3F1ZTMyS2lvc3F1ZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\n\r\n");
    out((const unsigned char *)q, (int)strlen(q));
}

/* Réponse « HTTP/1.1 101 ... » puis une ligne vide. Les octets qui suivent
   sont déjà des trames. */
static void http_byte(unsigned char c)
{
    if (h_pos < 12) {                       /* « HTTP/1.1 101 » : 101 en 9-11 */
        if (h_pos == 9 && c == '1') h_ok = 1;
        if ((h_pos == 10 && c != '0') || (h_pos == 11 && c != '1')) h_ok = 0;
        ++h_pos;
    }
    h_win[0] = h_win[1]; h_win[1] = h_win[2]; h_win[2] = h_win[3]; h_win[3] = c;
    if (h_win[0] == '\r' && h_win[1] == '\n' && h_win[2] == '\r' && h_win[3] == '\n')
        proto_state = h_ok ? PROTO_OPEN : PROTO_REFUSED;
}

/* --- interface ------------------------------------------------------- */

void proto_init(int k, proto_sink_fn s, proto_out_fn o, unsigned long seed)
{
    kind = k;
    sink = s;
    out = o;
    rnd = seed;
    t_state = 0;
    w_state = 0; w_op = 1; w_utf = 0; w_ping_len = 0;
    h_pos = 0; h_ok = 0;
    memset(h_win, 0, sizeof(h_win));
    proto_state = (kind == NET_WS) ? PROTO_HANDSHAKE : PROTO_OPEN;
}

void proto_feed(const unsigned char *p, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        if (proto_state == PROTO_HANDSHAKE) http_byte(p[i]);
        else if (proto_state != PROTO_OPEN) return;
        else if (kind == NET_WS) ws_byte(p[i]);
        else if (kind == NET_TELNET) telnet_byte(p[i]);
        else sink(p[i]);
    }
}

void proto_send(const unsigned char *p, int n)
{
    if (n <= 0 || proto_state != PROTO_OPEN)
        return;
    if (kind == NET_WS) {
        ws_frame(1, p, n);
    } else if (kind == NET_TELNET) {
        unsigned char b[256];
        int i, k = 0;
        for (i = 0; i < n; ++i) {
            if (k >= (int)sizeof(b) - 2) { out(b, k); k = 0; }
            if (p[i] == IAC) b[k++] = IAC;
            b[k++] = p[i];
        }
        out(b, k);
    } else {
        out(p, n);
    }
}

void proto_close(void)
{
    if (kind == NET_WS && proto_state == PROTO_OPEN)
        ws_frame(8, (const unsigned char *)"\x03\xE8", 2);  /* fermeture normale */
    proto_state = PROTO_CLOSED;
}
