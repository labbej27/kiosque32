/* Kiosque 32 : image de l'écran Minitel, 320x250 points, 8 bits par point
   (index de couleur : 0-7 couleurs du Minitel, 8-15 niveaux de gris). */
#ifndef KIOSQUE_FB_H
#define KIOSQUE_FB_H

#define FB_W 320
#define FB_H 250

/* drapeaux de fb_cell */
#define F_DW    0x01            /* double largeur */
#define F_RIGHT 0x02            /* moitié droite (si double largeur) */
#define F_DH    0x04            /* double hauteur */
#define F_TOP   0x08            /* moitié haute (si double hauteur) */
#define F_UL    0x10            /* souligné */

extern unsigned char fb[FB_H][FB_W];
extern int fb_dirty_top, fb_dirty_bottom;   /* rangées à redessiner, -1 = rien */

void fb_cell(int row, int col, const unsigned char *src, unsigned char flags,
             unsigned char fg, unsigned char bg);
void fb_clear_rows(int first, int count, unsigned char color);
void fb_copy_row(int from, int to);

#endif
