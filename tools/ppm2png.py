#!/usr/bin/env python3
"""ppm2png.py entrée.ppm sortie.png [échelle] : conversion sans dépendance."""
import sys, zlib, struct
data = open(sys.argv[1], "rb").read()
parts = data.split(maxsplit=4)
w, h = int(parts[1]), int(parts[2])
px = parts[4]
k = int(sys.argv[3]) if len(sys.argv) > 3 else 2
raw = bytearray()
for y in range(h):
    row = px[y * w * 3:(y + 1) * w * 3]
    line = bytearray()
    for x in range(w):
        line += row[x * 3:x * 3 + 3] * k
    for _ in range(k):
        raw += b"\0" + line
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * k, h * k, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
