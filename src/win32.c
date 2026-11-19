/* Kiosque 32 : terminal Minitel pour Windows (95, 98, NT 4, 2000, XP et
   suivants), en telnet et en WebSocket (ws://).
   (c) 2026 LABBEJ27 - licence MIT.

   Interface Win32 seule (user32, gdi32), réseau Winsock 1.1 (wsock32)
   en mode asynchrone (WSAAsyncSelect), aucune bibliothèque C. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include "mini.h"
#include "ids.h"
#include "fb.h"
#include "vdt.h"
#include "proto.h"

#define APP_NAME   "Kiosque 32"
#define WM_SOCK    (WM_APP + 1)
#define WM_HOST    (WM_APP + 2)
#define TIMER_TICK 1
#define TIMER_WS   2
#define BAR_H      30
#define MIN_W      640

/* --- carnet d'adresses (le même que Kiosque 64) ------------------------ */
struct site {
    const char *name, *host;
    int port;
    const char *path;
    int kind;
    int echo;          /* écho local au départ (le serveur peut le changer par PRO3) */
};
static const struct site sites[5] = {
    /* 3611.re et 3615co.de renvoient eux-mêmes les frappes (comme le
       faisait le réseau Transpac) ; HACKER compte sur l'écho du Minitel. */
    { "MiniPAVI", "go.minipavi.fr", 516, "", NET_TELNET, 1 },
    { "LABBEJ27", "minitel.labbej.fr", 8180, "", NET_TELNET, 1 },
    { "3615 co.de", "3615co.de", 80, "/ws", NET_WS, 0 },
    { "3611.re", "3611.re", 80, "/ws", NET_WS, 0 },
    { "HACKER", "mntl.joher.com", 2018, "/", NET_WS, 1 },
};

/* --- état ------------------------------------------------------------ */
enum { ST_HOME, ST_RESOLVING, ST_CONNECTING, ST_HANDSHAKE, ST_ONLINE, ST_ENDED };
static int state = ST_HOME;
static HWND hwnd;
static HINSTANCE hinst;
static HWND buttons[9];
static int scale = 2, show_bar = 1, grey = 0;
static int eat_char;                   /* touche qui a servi à quitter l'écran de fin */
static int cur_vis = 1, ticks = 0, cur_row = -1, cur_col = -1;
static char ini_path[MAX_PATH];

static SOCKET sock = INVALID_SOCKET;
static HANDLE host_req;
static char host_buf[MAXGETHOSTSTRUCT];
static char c_host[128], c_path[128], c_label[160];
static int c_port, c_kind, c_echo = 1, echo_pref = 1;
static char last_addr[200];

static unsigned char outq[8192];
static int outq_len;

/* --- petites aides ----------------------------------------------------- */

static void put_num(char *q, int n)
{
    char d[12];
    int i = 0;
    q += strlen(q);
    do { d[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i) *q++ = d[--i];
    *q = 0;
}

static int to_int(const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

static int starts(const char *s, const char *p)
{
    while (*p) {
        char a = *s++, b = *p++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != b) return 0;
    }
    return 1;
}

/* Caractère Windows-1252 -> octets Vidéotex (SS2 pour les accents).
   Retourne le nombre d'octets, 0 si le caractère n'existe pas. */
static int latin_to_vdt(unsigned char c, unsigned char *o)
{
    static const char from[] = "\xE0\xE2\xE4\xE7\xE8\xE9\xEA\xEB\xEE\xEF\xF4\xF6\xF9\xFB\xFC"
                               "\xC0\xC2\xC4\xC7\xC8\xC9\xCA\xCB\xCE\xCF\xD4\xD6\xD9\xDB\xDC";
    static const char acc[] = "ACHKABCHCHCHACH";
    static const char base[] = "aaaceeeeiioouuu";
    int i;
    if (c >= 0x20 && c < 0x7F) { o[0] = c; return 1; }
    for (i = 0; from[i]; ++i) {
        if ((unsigned char)from[i] == c) {
            int k = i % 15;
            o[0] = 0x19;
            o[1] = 0x40 + (acc[k] - '@');
            o[2] = (i < 15) ? base[k] : base[k] - 32;
            /* SS2 + A grave, B aigu, C circonflexe, H tréma, K cédille */
            return 3;
        }
    }
    switch (c) {
    case 0xA3: o[0] = 0x19; o[1] = 0x23; return 2;   /* £ */
    case 0xB0: o[0] = 0x19; o[1] = 0x30; return 2;   /* ° */
    case 0xB1: o[0] = 0x19; o[1] = 0x31; return 2;   /* ± */
    case 0xF7: o[0] = 0x19; o[1] = 0x38; return 2;   /* ÷ */
    case 0xBC: o[0] = 0x19; o[1] = 0x3C; return 2;   /* ¼ */
    case 0xBD: o[0] = 0x19; o[1] = 0x3D; return 2;   /* ½ */
    case 0xBE: o[0] = 0x19; o[1] = 0x3E; return 2;   /* ¾ */
    case 0x8C: o[0] = 0x19; o[1] = 0x6A; return 2;   /* Œ */
    case 0x9C: o[0] = 0x19; o[1] = 0x7A; return 2;   /* œ */
    }
    return 0;
}

/* Écrit du texte Windows-1252 sur l'écran Minitel. */
static void vtext(const char *s)
{
    unsigned char o[4];
    while (*s) {
        int n = latin_to_vdt((unsigned char)*s++, o);
        vdt_write(o, n);
    }
}

static void vat(int row, int col)       /* col de 1 à 40 */
{
    unsigned char o[3] = { 0x1F, 0x40 + row, 0x40 + col };
    vdt_write(o, 3);
}

static void vesc(unsigned char c)
{
    unsigned char o[2] = { 0x1B, c };
    vdt_write(o, 2);
}

/* Message en rangée 0 (la ligne de service du Minitel). */
static void status(const char *s)
{
    vat(0, 1);
    vtext(s);
    vdt_put(0x18);                      /* efface la fin de la rangée */
    vdt_put(0x0A);                      /* retour à la position d'avant */
}

/* --- fenêtre ------------------------------------------------------------ */

static int screen_w(void) { return FB_W * scale; }
static int client_w(void) { return screen_w() > MIN_W ? screen_w() : MIN_W; }
static int screen_x(void) { return (client_w() - screen_w()) / 2; }

static void invalidate_rows(int r0, int r1)
{
    RECT r;
    r.left = screen_x();
    r.right = r.left + screen_w();
    r.top = r0 * 10 * scale;
    r.bottom = (r1 + 1) * 10 * scale;
    InvalidateRect(hwnd, &r, FALSE);
}

static void flush_screen(void)
{
    if (fb_dirty_top >= 0) {
        invalidate_rows(fb_dirty_top, fb_dirty_bottom);
        fb_dirty_top = fb_dirty_bottom = -1;
    }
    vdt_cursor_update();
    if (vdt_row != cur_row || vdt_col != cur_col) {
        if (cur_row >= 0) invalidate_rows(cur_row, cur_row);
        cur_row = vdt_row;
        cur_col = vdt_col;
        cur_vis = 1;
        invalidate_rows(cur_row, cur_row);
    }
}

static const char *fkey_label[9] = {
    "Sommaire", "Annulation", "Retour", "Répétition", "Guide",
    "Correction", "Suite", "Envoi", "Cnx/Fin"
};
static const char fkey_code[9] = { 'F', 'E', 'B', 'C', 'D', 'G', 'H', 'A', 'I' };

static void layout(void)
{
    RECT r;
    int i, w = client_w(), h = FB_H * scale + (show_bar ? BAR_H : 0);
    r.left = 0; r.top = 0; r.right = w; r.bottom = h;
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE);
    SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    /* le menu peut passer sur deux lignes : on corrige avec la vraie zone */
    GetClientRect(hwnd, &r);
    if (r.bottom != h) {
        RECT wr;
        GetWindowRect(hwnd, &wr);
        SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top + h - r.bottom,
                     SWP_NOMOVE | SWP_NOZORDER);
    }
    for (i = 0; i < 9; ++i) {
        int x0 = 4 + (w - 8) * i / 9, x1 = 4 + (w - 8) * (i + 1) / 9;
        MoveWindow(buttons[i], x0 + 1, FB_H * scale + 3, x1 - x0 - 2, BAR_H - 6, TRUE);
        ShowWindow(buttons[i], show_bar ? SW_SHOW : SW_HIDE);
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

static void update_menu(void)
{
    HMENU m = GetMenu(hwnd);
    int i;
    CheckMenuItem(m, IDC_COLOR, grey ? MF_UNCHECKED : MF_CHECKED);
    CheckMenuItem(m, IDC_GREY, grey ? MF_CHECKED : MF_UNCHECKED);
    for (i = 1; i <= 3; ++i)
        CheckMenuItem(m, IDC_SCALE + i, scale == i ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDC_BAR, show_bar ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDC_ECHO, (state == ST_ONLINE ? vdt_echo : echo_pref) ? MF_CHECKED : MF_UNCHECKED);
    EnableMenuItem(m, IDC_HANGUP, state == ST_HOME ? MF_GRAYED : MF_ENABLED);
}

static void set_title(void)
{
    char t[256];
    strcpy(t, APP_NAME);
    if (state != ST_HOME && state != ST_ENDED) {
        strcat(t, " - ");
        strcat(t, c_label);
    }
    SetWindowTextA(hwnd, t);
    update_menu();
}

static void save_settings(void)
{
    char n[8];
    n[0] = 0; put_num(n, scale);
    WritePrivateProfileStringA("kiosque", "taille", n, ini_path);
    n[0] = 0; put_num(n, grey);
    WritePrivateProfileStringA("kiosque", "gris", n, ini_path);
    n[0] = 0; put_num(n, show_bar);
    WritePrivateProfileStringA("kiosque", "barre", n, ini_path);
    n[0] = 0; put_num(n, echo_pref);
    WritePrivateProfileStringA("kiosque", "echo", n, ini_path);
    WritePrivateProfileStringA("kiosque", "adresse", last_addr, ini_path);
}

static void load_settings(void)
{
    char *p;
    GetModuleFileNameA(NULL, ini_path, sizeof(ini_path) - 16);
    p = ini_path + strlen(ini_path);
    while (p > ini_path && p[-1] != '\\') --p;
    strcpy(p, "kiosque32.ini");
    scale = GetPrivateProfileIntA("kiosque", "taille", 2, ini_path);
    if (scale < 1 || scale > 3) scale = 2;
    grey = GetPrivateProfileIntA("kiosque", "gris", 0, ini_path) != 0;
    echo_pref = GetPrivateProfileIntA("kiosque", "echo", 1, ini_path) != 0;
    show_bar = GetPrivateProfileIntA("kiosque", "barre", 1, ini_path) != 0;
    GetPrivateProfileStringA("kiosque", "adresse", "go.minipavi.fr:516", last_addr,
                             sizeof(last_addr), ini_path);
}

/* --- page d'accueil ----------------------------------------------------- */

static void home_page(void)
{
    static const char *desc[5] = {
        "telnet  go.minipavi.fr",
        "telnet  minitel.labbej.fr",
        "ws      3615co.de",
        "ws      3611.re",
        "ws      mntl.joher.com",
    };
    int i;
    state = ST_HOME;
    vdt_reset();
    vdt_grey = (unsigned char)grey;
    vdt_put(0x0C);
    /* bandeau bleu */
    for (i = 1; i <= 4; ++i) {
        vat(i, 1);
        vdt_put(0x0E);                  /* semi-graphique : fond en case */
        vesc(0x54);                     /* fond bleu */
        vdt_put(0x20);
        vdt_put(0x12); vdt_put(0x40 + 39);
        vdt_put(0x0F);
    }
    vat(3, 11);
    vesc(0x54); vesc(0x4F); vesc(0x47);  /* fond bleu, double taille, blanc */
    vtext(" KIOSQUE 32");
    vat(4, 7);
    vesc(0x54); vesc(0x46);
    vtext(" Terminal Minitel pour Windows");
    vat(6, 1);
    vesc(0x46);
    vtext("Telnet et WebSocket, de Windows 95 à");
    vat(7, 1);
    vesc(0x46);
    vtext("aujourd'hui.");
    for (i = 0; i < 5; ++i) {
        char n[2] = { (char)('1' + i), 0 };
        vat(9 + 2 * i, 3);
        vesc(0x43); vesc(0x5D);         /* jaune, inversé */
        vtext(" "); vtext(n); vtext(" ");
        vesc(0x5C); vesc(0x47);
        vtext(" ");
        vtext(sites[i].name);
        vat(9 + 2 * i, 19);
        vesc(0x42);
        vtext(desc[i]);
    }
    vat(19, 3);
    vesc(0x43); vesc(0x5D);
    vtext(" 6 ");
    vesc(0x5C); vesc(0x47);
    vtext(" Autre adresse...");
    vat(22, 1);
    vesc(0x44);
    vdt_put(0x60); vdt_put(0x12); vdt_put(0x40 + 39);
    vat(23, 2);
    vtext("Tapez le numéro du service.");
    vat(24, 2);
    vesc(0x46);
    vtext("F8 : déconnexion   Menu ? : les touches");
    set_title();
    flush_screen();
}

/* --- réseau --------------------------------------------------------------- */

static void sock_close(void)
{
    if (host_req) {
        WSACancelAsyncRequest(host_req);
        host_req = 0;
    }
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    outq_len = 0;
    KillTimer(hwnd, TIMER_WS);
}

static void flush_out(void)
{
    while (outq_len > 0 && sock != INVALID_SOCKET) {
        int n = send(sock, (const char *)outq, outq_len, 0);
        if (n == SOCKET_ERROR)
            return;                     /* WSAEWOULDBLOCK : FD_WRITE suivra */
        memmove(outq, outq + n, outq_len - n);
        outq_len -= n;
    }
}

static void net_out(const unsigned char *p, int n)
{
    if (outq_len + n > (int)sizeof(outq))
        n = (int)sizeof(outq) - outq_len;
    memcpy(outq + outq_len, p, n);
    outq_len += n;
    flush_out();
}

static void sink(unsigned char c)
{
    vdt_put(c);
}

/* Fin de connexion : message en rangée 0, une touche ramène à l'accueil. */
static void ended(const char *msg)
{
    sock_close();
    state = ST_ENDED;
    status(msg);
    set_title();
    flush_screen();
}

static void hang_up(void)
{
    if (state == ST_ONLINE || state == ST_HANDSHAKE) {
        proto_close();
        flush_out();
    }
    sock_close();
    home_page();
}

static void start_connect(unsigned long addr)
{
    struct sockaddr_in sa;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        ended("Winsock : impossible d'ouvrir une connexion.");
        return;
    }
    WSAAsyncSelect(sock, hwnd, WM_SOCK, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)c_port);
    sa.sin_addr.s_addr = addr;
    state = ST_CONNECTING;
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR
        && WSAGetLastError() != WSAEWOULDBLOCK)
        ended("Connexion refusée.");
}

static void connect_to(const char *host, int port, const char *path, int kind, int echo)
{
    unsigned long a;
    char m[200];
    if (state != ST_HOME && state != ST_ENDED)
        hang_up();
    if (state == ST_ENDED)
        home_page();
    strcpy(c_host, host);
    strcpy(c_path, path);
    c_port = port;
    c_kind = kind;
    c_echo = echo;
    strcpy(c_label, host);
    strcat(c_label, ":");
    put_num(c_label, port);
    if (kind == NET_WS) {
        strcat(c_label, c_path);
        strcat(c_label, " (WebSocket)");
    } else {
        strcat(c_label, " (telnet)");
    }
    strcpy(m, "Connexion à ");
    strcat(m, c_host);
    strcat(m, "...");
    status(m);
    state = ST_RESOLVING;
    set_title();
    flush_screen();
    a = inet_addr(host);
    if (a != INADDR_NONE) {
        start_connect(a);
        return;
    }
    host_req = WSAAsyncGetHostByName(hwnd, WM_HOST, host, host_buf, sizeof(host_buf));
    if (!host_req)
        ended("Winsock : recherche d'adresse impossible.");
}

static void on_host(LPARAM lp)
{
    struct hostent *h = (struct hostent *)host_buf;
    host_req = 0;
    if (state != ST_RESOLVING)
        return;
    if (WSAGETASYNCERROR(lp) || !h->h_addr_list[0]) {
        ended("Adresse inconnue (DNS).");
        return;
    }
    start_connect(*(unsigned long *)h->h_addr_list[0]);
}

static void after_data(void)
{
    if (vdt_reply_len) {
        proto_send(vdt_reply, vdt_reply_len);
        vdt_reply_len = 0;
    }
    if (vdt_bell) {
        MessageBeep(MB_OK);
        vdt_bell = 0;
    }
    flush_screen();
}

static void on_sock(WPARAM wp, LPARAM lp)
{
    int ev = WSAGETSELECTEVENT(lp), err = WSAGETSELECTERROR(lp);
    if ((SOCKET)wp != sock)
        return;
    switch (ev) {
    case FD_CONNECT:
        if (err) {
            ended("Le serveur ne répond pas.");
            return;
        }
        vdt_reset();
        vdt_grey = (unsigned char)grey;
        vdt_echo = (unsigned char)c_echo;
        flush_screen();
        proto_init(c_kind, sink, net_out, GetTickCount());
        if (c_kind == NET_WS) {
            state = ST_HANDSHAKE;
            proto_ws_open(c_host, c_port, c_path);
            SetTimer(hwnd, TIMER_WS, 8000, NULL);
        } else {
            state = ST_ONLINE;
        }
        set_title();
        return;
    case FD_WRITE:
        flush_out();
        return;
    case FD_READ:
    case FD_CLOSE: {
        static unsigned char b[4096];
        for (;;) {
            int n = recv(sock, (char *)b, sizeof(b), 0);
            if (n <= 0)
                break;
            proto_feed(b, n);
            if (ev == FD_READ)
                break;                  /* Winsock relance FD_READ s'il reste des octets */
        }
        if (state == ST_HANDSHAKE && proto_state == PROTO_OPEN) {
            KillTimer(hwnd, TIMER_WS);
            state = ST_ONLINE;
        }
        after_data();
        if (proto_state == PROTO_REFUSED)
            ended("Le serveur a refusé le WebSocket.");
        else if (ev == FD_CLOSE || proto_state == PROTO_CLOSED)
            ended("Fin de connexion. Une touche : accueil.");
        return;
    }
    }
}

/* Adresse tapée : [telnet://|ws://|tcp://]hôte[:port][/chemin] */
static void connect_address(const char *s)
{
    char host[128], path[128];
    int kind = NET_TELNET, port, i = 0;
    while (*s == ' ') ++s;
    if (starts(s, "wss://")) {
        MessageBoxA(hwnd, "Les adresses wss:// (WebSocket chiffré) ne sont pas encore\n"
                          "prises en charge. Essayez ws:// ou telnet.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    if (starts(s, "ws://")) { kind = NET_WS; s += 5; }
    else if (starts(s, "telnet://")) s += 9;
    else if (starts(s, "tcp://")) { kind = NET_TCP; s += 6; }
    port = (kind == NET_WS) ? 80 : 23;
    while (*s && *s != ':' && *s != '/' && *s != ' ' && i < (int)sizeof(host) - 1)
        host[i++] = *s++;
    host[i] = 0;
    if (*s == ':') {
        ++s;
        port = to_int(s);
        while (*s >= '0' && *s <= '9') ++s;
    }
    i = 0;
    while (*s && *s != ' ' && i < (int)sizeof(path) - 1)
        path[i++] = *s++;
    path[i] = 0;
    if (!host[0] || port <= 0 || port > 65535) {
        MessageBoxA(hwnd, "Adresse incomplète.", APP_NAME, MB_ICONEXCLAMATION);
        return;
    }
    connect_to(host, port, path[0] ? path : "/", kind, echo_pref);
}

static BOOL CALLBACK address_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextA(dlg, IDE_ADDRESS, last_addr);
        SendDlgItemMessageA(dlg, IDE_ADDRESS, EM_SETSEL, 0, -1);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            GetDlgItemTextA(dlg, IDE_ADDRESS, last_addr, sizeof(last_addr));
            save_settings();
            EndDialog(dlg, 1);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
    }
    return FALSE;
}

static void ask_address(void)
{
    if (DialogBoxParamA(hinst, MAKEINTRESOURCEA(IDD_ADDRESS), hwnd, (DLGPROC)address_proc, 0) == 1)
        connect_address(last_addr);
}

/* --- clavier ------------------------------------------------------------- */

static void send_keys(const unsigned char *o, int n)
{
    if (state != ST_ONLINE || n <= 0)
        return;
    proto_send(o, n);
    if (vdt_echo && o[0] != 0x13)
        vdt_write(o, n);
    flush_screen();
}

static void fkey(char code)
{
    unsigned char o[2];
    o[0] = 0x13;                        /* SEP + lettre de la touche de fonction */
    o[1] = (unsigned char)code;
    send_keys(o, 2);
}

static void home_choice(int k)
{
    if (k >= '1' && k <= '5') {
        const struct site *s = &sites[k - '1'];
        connect_to(s->host, s->port, s->path, s->kind, s->echo);
    } else if (k == '6') {
        ask_address();
    }
}

static int on_keydown(WPARAM vk)
{
    eat_char = 0;
    if (state == ST_ENDED && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
        home_page();
        eat_char = 1;
        return 1;
    }
    if (vk == VK_F8) {
        if (state != ST_HOME)
            hang_up();
        return 1;
    }
    if (state != ST_ONLINE)
        return 0;
    switch (vk) {
    case VK_F1: fkey('F'); return 1;    /* Sommaire */
    case VK_F2: fkey('D'); return 1;    /* Guide */
    case VK_F3: fkey('B'); return 1;    /* Retour */
    case VK_F4: fkey('C'); return 1;    /* Répétition */
    case VK_F5: fkey('H'); return 1;    /* Suite */
    case VK_F6: fkey('E'); return 1;    /* Annulation */
    case VK_F7: fkey('I'); return 1;    /* Connexion/Fin (avec Maj sur un Minitel) */
    case VK_RETURN: fkey('A'); return 1;/* Envoi */
    case VK_BACK: fkey('G'); return 1;  /* Correction */
    case VK_ESCAPE: fkey('E'); return 1;/* Annulation */
    case VK_DOWN: fkey('H'); return 1;  /* Suite */
    case VK_UP: fkey('B'); return 1;    /* Retour */
    case VK_LEFT: fkey('F'); return 1;  /* Sommaire */
    case VK_RIGHT: fkey('C'); return 1; /* Répétition */
    }
    return 0;
}

static void on_char(unsigned char c)
{
    unsigned char o[4];
    int n;
    if (eat_char) {
        eat_char = 0;
        return;
    }
    if (state == ST_HOME) {
        home_choice(c);
        return;
    }
    if (state != ST_ONLINE || c < 0x20)
        return;
    /* Le Minitel tape en majuscules tant que le serveur n'a pas demandé
       les minuscules (PRO2 iE) : Maj inverse la casse, comme sur le clavier
       du Minitel. */
    if (!vdt_lower) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    n = latin_to_vdt(c, o);
    send_keys(o, n);
}

/* --- dessin ---------------------------------------------------------------- */

static unsigned char dib[FB_H][FB_W];

static void paint(HDC dc)
{
    static const unsigned char rgb[16][3] = {
        {0,0,0},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255},
        /* niveaux de gris du Minitel : bleu 40 %, rouge 50 %, magenta 60 %,
           vert 70 %, cyan 80 %, jaune 90 % */
        {0,0,0},{128,128,128},{178,178,178},{230,230,230},{102,102,102},{153,153,153},{204,204,204},{255,255,255},
    };
    struct { BITMAPINFOHEADER h; RGBQUAD c[16]; } bi;
    RECT r;
    int y, x0 = screen_x();
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    for (y = 0; y < 16; ++y) {
        bi.c[y].rgbRed = rgb[y][0];
        bi.c[y].rgbGreen = rgb[y][1];
        bi.c[y].rgbBlue = rgb[y][2];
        bi.c[y].rgbReserved = 0;
    }
    memset(&bi.h, 0, sizeof(bi.h));
    bi.h.biSize = sizeof(bi.h);
    bi.h.biWidth = FB_W;
    bi.h.biHeight = FB_H;               /* image de bas en haut : la forme sûre */
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 8;
    bi.h.biCompression = BI_RGB;
    bi.h.biClrUsed = 16;
    for (y = 0; y < FB_H; ++y)
        memcpy(dib[FB_H - 1 - y], fb[y], FB_W);
    StretchDIBits(dc, x0, 0, screen_w(), FB_H * scale, 0, 0, FB_W, FB_H,
                  dib, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    /* curseur : pavé inversé, clignotant */
    if (state == ST_ONLINE && vdt_cursor_on && cur_vis && cur_row >= 0) {
        r.left = x0 + cur_col * 8 * scale;
        r.top = cur_row * 10 * scale;
        r.right = r.left + 8 * scale;
        r.bottom = r.top + 10 * scale;
        InvertRect(dc, &r);
    }
    /* marges noires et fond de la barre */
    if (x0 > 0) {
        r.left = 0; r.top = 0; r.right = x0; r.bottom = FB_H * scale;
        FillRect(dc, &r, black);
        r.left = x0 + screen_w(); r.right = client_w();
        FillRect(dc, &r, black);
    }
    if (show_bar) {
        r.left = 0; r.top = FB_H * scale; r.right = client_w(); r.bottom = r.top + BAR_H;
        FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
    }
}

/* --- messages -------------------------------------------------------------- */

static const char keys_help[] =
    "Touches du Minitel :\n\n"
    "Entrée\tEnvoi\n"
    "Retour arrière\tCorrection\n"
    "Échap\tAnnulation\n"
    "F1 ou flèche gauche\tSommaire\n"
    "F2\tGuide\n"
    "F3 ou flèche haut\tRetour\n"
    "F4 ou flèche droite\tRépétition\n"
    "F5 ou flèche bas\tSuite\n"
    "F6\tAnnulation\n"
    "F7\tConnexion/Fin (Maj + Cnx/Fin)\n"
    "F8\tDéconnexion\n\n"
    "Comme sur un Minitel, les lettres sortent en majuscules tant que le\n"
    "service n'a pas choisi les minuscules ; Maj inverse la casse.\n"
    "Les lettres accentuées sont envoyées telles quelles.";

static void command(int id)
{
    if (id >= IDC_FKEY && id < IDC_FKEY + 9) {
        if (state == ST_ONLINE)
            fkey(fkey_code[id - IDC_FKEY]);
        else if (state == ST_ENDED)
            home_page();
        SetFocus(hwnd);
        return;
    }
    if (id >= IDC_SITE && id < IDC_SITE + 5) {
        const struct site *s = &sites[id - IDC_SITE];
        connect_to(s->host, s->port, s->path, s->kind, s->echo);
        return;
    }
    if (id >= IDC_SCALE + 1 && id <= IDC_SCALE + 3) {
        scale = id - IDC_SCALE;
        save_settings();
        update_menu();
        layout();
        return;
    }
    switch (id) {
    case IDC_OTHER: ask_address(); break;
    case IDC_HANGUP: if (state != ST_HOME) hang_up(); break;
    case IDC_QUIT: DestroyWindow(hwnd); break;
    case IDC_COLOR:
    case IDC_GREY:
        grey = id == IDC_GREY;
        vdt_grey = (unsigned char)grey;
        vdt_redraw();
        save_settings();
        update_menu();
        flush_screen();
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case IDC_ECHO:
        /* en ligne : change l'écho de la session ; sinon, le réglage des
           « autres adresses » */
        if (state == ST_ONLINE) {
            vdt_echo = !vdt_echo;
        } else {
            echo_pref = !echo_pref;
            save_settings();
        }
        update_menu();
        break;
    case IDC_BAR:
        show_bar = !show_bar;
        save_settings();
        update_menu();
        layout();
        break;
    case IDC_KEYS:
        MessageBoxA(hwnd, keys_help, APP_NAME, MB_ICONINFORMATION);
        break;
    case IDC_ABOUT:
        MessageBoxA(hwnd,
            "Kiosque 32, version 0.1\n"
            "Terminal Minitel en telnet et en WebSocket (ws://)\n"
            "pour Windows 95, 98, NT 4, 2000, XP et suivants.\n\n"
            "(c) 2026 LABBEJ27 - licence MIT\n"
            "Police : unscii de viznut (domaine public).\n\n"
            "« Minitel » est une marque d'Orange ; Kiosque 32\n"
            "n'est pas lié à Orange.",
            APP_NAME, MB_ICONINFORMATION);
        break;
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int i;
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        for (i = 0; i < 9; ++i) {
            buttons[i] = CreateWindowA("BUTTON", fkey_label[i],
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       0, 0, 10, 10, h, (HMENU)(INT_PTR)(IDC_FKEY + i), hinst, NULL);
            SendMessageA(buttons[i], WM_SETFONT, (WPARAM)f, 0);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (on_keydown(wp))
            return 0;
        break;
    case WM_CHAR:
        on_char((unsigned char)wp);
        return 0;
    case WM_COMMAND:
        command(LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == TIMER_WS) {
            KillTimer(h, TIMER_WS);
            if (state == ST_HANDSHAKE)
                ended("Le serveur WebSocket ne répond pas.");
        } else if (wp == TIMER_TICK) {
            ++ticks;
            if (ticks % 5 == 0) {       /* curseur : 0,5 s */
                cur_vis ^= 1;
                if (cur_row >= 0 && vdt_cursor_on && state == ST_ONLINE)
                    invalidate_rows(cur_row, cur_row);
            }
            if (ticks % 6 == 0) {       /* attributs clignotants : 0,6 s */
                vdt_blink_tick();
                flush_screen();
            }
        }
        return 0;
    case WM_SOCK:
        on_sock(wp, lp);
        return 0;
    case WM_HOST:
        on_host(lp);
        return 0;
    case WM_INITMENUPOPUP:
        update_menu();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE)
            SetFocus(h);
        return 0;
    case WM_DESTROY:
        if (state == ST_ONLINE)
            proto_close();
        flush_out();
        sock_close();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* --- démarrage --------------------------------------------------------------- */

static int run(void)
{
    WNDCLASSA wc;
    WSADATA wsa;
    MSG m;
    const char *cmd;
    hinst = GetModuleHandleA(NULL);
    if (WSAStartup(MAKEWORD(1, 1), &wsa)) {
        MessageBoxA(NULL, "Winsock (réseau TCP/IP) n'est pas installé.", APP_NAME, MB_ICONSTOP);
        return 1;
    }
    load_settings();
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconA(hinst, MAKEINTRESOURCEA(IDI_KIOSQUE));
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = MAKEINTRESOURCEA(IDM_MAIN);
    wc.lpszClassName = "Kiosque32";
    RegisterClassA(&wc);
    hwnd = CreateWindowA("Kiosque32", APP_NAME,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         CW_USEDEFAULT, CW_USEDEFAULT, 660, 580, NULL, NULL, hinst, NULL);
    layout();
    home_page();
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    SetTimer(hwnd, TIMER_TICK, 100, NULL);

    /* kiosque32.exe [adresse] : connexion directe */
    cmd = GetCommandLineA();
    if (*cmd == '"') { ++cmd; while (*cmd && *cmd != '"') ++cmd; if (*cmd) ++cmd; }
    else while (*cmd && *cmd != ' ') ++cmd;
    while (*cmd == ' ') ++cmd;
    if (*cmd)
        connect_address(cmd);

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    WSACleanup();
    return 0;
}

/* Point d'entrée du programme (pas de bibliothèque C, donc pas de WinMain). */
void entry(void)
{
    ExitProcess((UINT)run());
}
