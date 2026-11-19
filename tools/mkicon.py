#!/usr/bin/env python3
"""mkicon.py sortie.ico : icône 32x32 et 16x16 en 16 couleurs (format
lisible par Windows 95), un petit terminal beige à écran bleu."""
import struct, sys

PAL = [(0,0,0),(128,0,0),(0,128,0),(128,128,0),(0,0,128),(128,0,128),(0,128,128),(192,192,192),
       (128,128,128),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255)]
BEIGE, DARK, SCREEN, TEXT, YEL = 7, 8, 4, 15, 11

def draw(n):
    img = [[None] * n for _ in range(n)]
    k = n / 32
    def rect(x0, y0, x1, y1, c):
        for y in range(int(y0 * k), int(y1 * k)):
            for x in range(int(x0 * k), int(x1 * k)):
                img[y][x] = c
    rect(2, 3, 30, 25, DARK)          # boîtier
    rect(3, 4, 29, 24, BEIGE)
    rect(6, 6, 26, 20, 0)             # écran
    rect(7, 7, 25, 19, SCREEN)
    for i, y in enumerate((9, 12, 15)):
        rect(9, y, 23 - 4 * i, y + 1.5, TEXT if i else YEL)
    rect(4, 25, 28, 29, DARK)         # clavier
    rect(5, 26, 27, 28, BEIGE)
    return img

def bmp(n):
    img = draw(n)
    hdr = struct.pack("<IiiHHIIiiII", 40, n, 2 * n, 1, 4, 0, 0, 0, 0, 16, 0)
    pal = b"".join(struct.pack("BBBB", b, g, r, 0) for r, g, b in PAL)
    xor = b""
    for y in reversed(range(n)):
        row = [img[y][x] or 0 for x in range(n)]
        xor += bytes((row[i] << 4) | row[i + 1] for i in range(0, n, 2))
    stride = ((n + 31) // 32) * 4
    andm = b""
    for y in reversed(range(n)):
        bits = 0
        for x in range(n):
            bits = (bits << 1) | (img[y][x] is None)
        andm += bits.to_bytes(n // 8, "big") + b"\0" * (stride - n // 8)
    return hdr + pal + xor + andm

imgs = [(32, bmp(32)), (16, bmp(16))]
out = struct.pack("<HHH", 0, 1, len(imgs))
off = 6 + 16 * len(imgs)
for n, d in imgs:
    out += struct.pack("<BBBBHHII", n, n, 16, 0, 1, 4, len(d), off)
    off += len(d)
out += b"".join(d for _, d in imgs)
open(sys.argv[1], "wb").write(out)
