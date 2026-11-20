#!/usr/bin/env python3
"""Police de Kiosque 32 : jeu G0 du Minitel en cases de 8x10 (comme le
vrai Minitel, 320x250 points), à partir d'unscii-8 (domaine public,
viznut.fi/unscii), et semi-graphiques G1 2x3.

mkfont.py unscii-8.hex sortie.c

Codes internes (128 caractères), les mêmes que Kiosque 64 :
  $20-$7F  ASCII, avec les particularités du Minitel ($5E flèche haut,
           $60 trait horizontal, $7F pavé plein) ;
  $00-$1F  caractères obtenus par SS2 (accents, £, °, flèches, fractions).
Le glyphe 8x8 est posé sur les lignes 1 à 8 de la case ; la ligne 9 sert
au soulignement.
"""
import sys

glyphs = {}
for line in open(sys.argv[1]):
    code, data = line.strip().split(":")
    if len(data) == 16:
        glyphs[int(code, 16)] = bytes.fromhex(data)

# Accents redessinés comme dans Plume : unscii les trace à plat.
DRAWN = {
    "aigu": ["....##..", "...##..."],
    "grave": ["..##....", "...##..."],
    "circ": ["...##...", "..#..#.."],
}
REDRAW = {"é": ("e", "aigu"), "è": ("e", "grave"), "ê": ("e", "circ"), "à": ("a", "grave"),
          "â": ("a", "circ"), "ù": ("u", "grave"), "û": ("u", "circ"), "ô": ("o", "circ"),
          "î": ("i", "circ")}
for ch, (base, accent) in REDRAW.items():
    rows = bytearray(glyphs[ord(base)])
    if base == "i":
        rows[0] = 0
    for r, line in enumerate(DRAWN[accent]):
        rows[r] = int(line.replace("#", "1").replace(".", "0"), 2)
    glyphs[ord(ch)] = bytes(rows)


def bits(*lines):
    return bytes(int(l.replace("#", "1").replace(".", "0"), 2) for l in lines)


SPECIAL = {
    0x5E: bits("...#....", "..###...", ".#.#.#..", "...#....", "...#....", "...#....", "...#....", "........"),
}

# Traits du Minitel, sur toute la case de 8x10 pour se raccorder aux cases
# voisines (les serveurs s'en servent pour dessiner des cadres et des logos,
# par exemple celui de HACKER) : 0x5F trait en bas, 0x60 trait au milieu,
# 0x7B trait à gauche, 0x7C trait vertical au milieu, 0x7D trait à droite,
# 0x7E trait en haut.
FULL = {
    0x5F: bytes([0] * 9 + [0xFF]),
    0x60: bytes([0] * 4 + [0xFF] + [0] * 5),
    0x7B: bytes([0x80] * 10),
    0x7C: bytes([0x18] * 10),
    0x7D: bytes([0x01] * 10),
    0x7E: bytes([0xFF] + [0] * 9),
}
LOW = "\x20ÂÉ£âÇËÀùÈŒÊ←î→↓°±éëïçûà÷èœê¼½¾ô"   # codes $00 à $1F

g0 = []
for code in range(128):
    if code == 0x7F:
        g0.append(bytes([0xFF] * 10))
        continue
    if code in FULL:
        g0.append(FULL[code])
        continue
    if code < 32:
        g = glyphs.get(ord(LOW[code]))
    elif code in SPECIAL:
        g = SPECIAL[code]
    else:
        g = glyphs.get(code)
    if g is None:
        sys.exit(f"glyphe manquant : code ${code:02X}")
    g0.append(bytes([0]) + g + bytes([0]))


def mosaic(v, separated):
    """6 pavés (bits 0-5), 2 colonnes x 3 bandes de 3, 4 et 3 lignes.
    En mode disjoint, chaque pavé perd sa dernière colonne et sa dernière
    ligne."""
    rows = []
    bands = [(0, 3), (3, 7), (7, 10)]
    for y in range(10):
        band = next(i for i, (a, b) in enumerate(bands) if a <= y < b)
        a, b = bands[band]
        byte = 0
        for side in (0, 1):
            if v >> (band * 2 + side) & 1:
                if separated and y == b - 1:
                    continue
                half = 0xF0 if side == 0 else 0x0F
                if separated:
                    half &= 0xE0 if side == 0 else 0x0E
                byte |= half
        rows.append(byte)
    return bytes(rows)


def table(name, items, comment):
    out = [f"const unsigned char {name}[{len(items)} * 10] = {{  /* {comment} */"]
    for i, g in enumerate(items):
        out.append("    " + ",".join(f"0x{b:02X}" for b in g) + f",  /* ${i:02X} */")
    out.append("};\n")
    return "\n".join(out)


with open(sys.argv[2], "w") as f:
    f.write("/* Généré par tools/mkfont.py depuis unscii-8 (domaine public). */\n")
    f.write('#include "font.h"\n\n')
    f.write(table("font_g0", g0, "jeu G0, 128 caractères"))
    f.write(table("font_g1", [mosaic(v, False) for v in range(64)], "semi-graphiques joints"))
    f.write(table("font_g1s", [mosaic(v, True) for v in range(64)], "semi-graphiques disjoints"))
