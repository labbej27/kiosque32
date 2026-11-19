/* Kiosque 32 : dessin des cases dans l'image de l'écran. */
#include "fb.h"

unsigned char fb[FB_H][FB_W];
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
    for (y = 0; y < 10; ++y) {
        unsigned char *p = &fb[row * 10 + y][col * 8];
        unsigned char b = g[y];
        for (x = 0; x < 8; ++x)
            p[x] = (b & (0x80 >> x)) ? fg : bg;
    }
    dirty(row, row);
}

void fb_clear_rows(int first, int count, unsigned char color)
{
    int y, x;
    for (y = first * 10; y < (first + count) * 10 && y < FB_H; ++y)
        for (x = 0; x < FB_W; ++x)
            fb[y][x] = color;
    dirty(first, first + count - 1);
}

void fb_copy_row(int from, int to)
{
    int y, x;
    for (y = 0; y < 10; ++y)
        for (x = 0; x < FB_W; ++x)
            fb[to * 10 + y][x] = fb[from * 10 + y][x];
    dirty(to, to);
}
