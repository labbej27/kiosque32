/* Banc d'essai sur Mac/Linux : se connecte à un serveur, décode et
   enregistre l'écran en PPM.
   headless hôte port telnet|ws|tcp chemin sortie.ppm secondes [touches...]
   Chaque argument « touches » est envoyé 3 s après le précédent ;
   \xHH est accepté (ex. "\x13A" = Envoi). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include "../src/fb.h"
#include "../src/vdt.h"
#include "../src/proto.h"

static int sock;
static const unsigned char pal[16][3] = {
    {0,0,0},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255},
    {0,0,0},{128,128,128},{178,178,178},{230,230,230},{102,102,102},{153,153,153},{204,204,204},{255,255,255},
};

static void out(const unsigned char *p, int n) { if (write(sock, p, n) != n) perror("write"); }
static FILE *dump;
static void sink(unsigned char c) { if (dump) fputc(c, dump); vdt_put(c); }

static int unescape(const char *s, unsigned char *o)
{
    int n = 0;
    while (*s) {
        if (s[0] == '\\' && s[1] == 'x') { unsigned v; sscanf(s + 2, "%2x", &v); o[n++] = v; s += 4; }
        else o[n++] = *s++;
    }
    return n;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
    struct addrinfo hints = {0}, *ai;
    int kind, ki = 7, x, y;
    double t0, tk;
    FILE *f;
    if (argc < 7) { fprintf(stderr, "usage\n"); return 2; }
    kind = !strcmp(argv[3], "ws") ? NET_WS : !strcmp(argv[3], "tcp") ? NET_TCP : NET_TELNET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(argv[1], argv[2], &hints, &ai)) { fprintf(stderr, "dns\n"); return 1; }
    sock = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(sock, ai->ai_addr, ai->ai_addrlen)) { perror("connect"); return 1; }
    if (getenv("DUMP")) dump = fopen(getenv("DUMP"), "wb");
    vdt_reset();
    proto_init(kind, sink, out, 1234);
    if (kind == NET_WS) proto_ws_open(argv[1], (unsigned)atoi(argv[2]), argv[4]);
    t0 = tk = now();
    while (now() - t0 < atof(argv[6])) {
        struct pollfd p = { sock, POLLIN, 0 };
        if (poll(&p, 1, 50) > 0) {
            unsigned char b[4096];
            int n = read(sock, b, sizeof b);
            if (n <= 0) { fprintf(stderr, "fermé\n"); break; }
            proto_feed(b, n);
            if (proto_state == PROTO_REFUSED) { fprintf(stderr, "ws refusé\n"); return 1; }
        }
        if (vdt_reply_len) { proto_send(vdt_reply, vdt_reply_len); vdt_reply_len = 0; }
        if (ki < argc && now() - tk > 3) {
            unsigned char k[64];
            int n = unescape(argv[ki++], k);
            proto_send(k, n);
            if (vdt_echo && !getenv("NOECHO") && k[0] != 0x13) vdt_write(k, n);
            tk = now();
        }
    }
    if (dump) fclose(dump);
    f = fopen(argv[5], "wb");
    fprintf(f, "P6 %d %d 255\n", FB_W, FB_H);
    for (y = 0; y < FB_H; ++y)
        for (x = 0; x < FB_W; ++x)
#ifdef FB_4BPP          /* image de Kiosque 16 : 4 bits, de bas en haut */
            fwrite(pal[(fb[FB_H - 1 - y][x / 2] >> ((x & 1) ? 0 : 4)) & 15], 1, 3, f);
#else
            fwrite(pal[fb[y][x]], 1, 3, f);
#endif
    fclose(f);
    printf("état %d, écho %d, minuscules %d\n", proto_state, vdt_echo, vdt_lower);
    return 0;
}
