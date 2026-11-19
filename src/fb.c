/* Kiosque 32 : dessin des cases dans l'image de l'écran. */
#include "mini.h"
#include "fb.h"

#ifdef FB_4BPP
unsigned char fb[FB_H][FB_W / 2];
#define LINE(y) fb[FB_H - 1 - (y)]

/* 8 points d'une ligne : bit à 1 = fg, à 0 = bg */
static void put8(int y, int col, unsigned char b, unsigned char fg, unsigned char bg)
{
    unsigned char *p = &LINE(y)[col * 4];
    int x;
    for (x = 0; x < 4; ++x, b <<= 2)
        p[x] = (unsigned char)((((b & 0x80) ? fg : bg) << 4) | ((b & 0x40) ? fg : bg));
}
#else
unsigned char fb[FB_H][FB_W];
#define LINE(y) fb[y]

static void put8(int y, int col, unsigned char b, unsigned char fg, unsigned char bg)
{
    unsigned char *p = &fb[y][col * 8];
    int x;
    for (x = 0; x < 8; ++x)
        p[x] = (b & (0x80 >> x)) ? fg : bg;
}
#endif
#define LINE_BYTES sizeof(fb[0])
int fb_dirty_top = -1, fb_dirty_bottom = -1;

static void dirty(int r0, int r1)
{
    if (fb_dirty_top < 0 || r0 < fb_dirty_top) fb_dirty_top = r0;
    if (r1 > fb_dirty_bottom) fb_dirty_bottom = r1;
}

void fb_cell(int row, int col, const unsigned char *src, unsigned char flags,
             unsigned char fg, unsigned char bg)
{
    unsigned char g[10];
    int y, x;
    if (row < 0 || row > 24 || col < 0 || col > 39)
        return;
    if (flags & F_DH) {             /* lignes 0-4 ou 5-9 doublées */
        int s = (flags & F_TOP) ? 0 : 5;
        for (y = 0; y < 10; ++y)
            g[y] = src[s + y / 2];
    } else {
        for (y = 0; y < 10; ++y)
            g[y] = src[y];
    }
    if (flags & F_DW) {             /* un quartet -> un octet, points doublés */
        for (y = 0; y < 10; ++y) {
            unsigned char n = (flags & F_RIGHT) ? (g[y] & 0x0F) : (g[y] >> 4), o = 0;
            for (x = 0; x < 4; ++x)
                if (n & (8 >> x)) o |= 0xC0 >> (2 * x);
            g[y] = o;
        }
    }
    /* soulignement : dernière ligne ; en double hauteur, moitié basse */
    if ((flags & F_UL) && (flags & (F_DH | F_TOP)) != (F_DH | F_TOP))
        g[9] = 0xFF;
    for (y = 0; y < 10; ++y)
        put8(row * 10 + y, col, g[y], fg, bg);
    dirty(row, row);
}

void fb_clear_rows(int first, int count, unsigned char color)
{
    int y;
#ifdef FB_4BPP
    color = (unsigned char)((color << 4) | color);
#endif
    for (y = first * 10; y < (first + count) * 10 && y < FB_H; ++y)
        memset(LINE(y), color, LINE_BYTES);
    dirty(first, first + count - 1);
}

void fb_copy_row(int from, int to)
{
    int y;
    for (y = 0; y < 10; ++y)
        memcpy(LINE(to * 10 + y), LINE(from * 10 + y), LINE_BYTES);
    dirty(to, to);
}
