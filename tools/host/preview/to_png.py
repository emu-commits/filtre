#!/usr/bin/env python3
"""PPM screen dumps -> PNG, scaled up, plus one contact sheet per screen."""
import sys, os, zlib, struct, glob

def read_ppm(p):
    d = open(p, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

def write_png(p, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, c): return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    open(p, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                        + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

def scale(w, h, rgb, k):
    out = bytearray()
    for y in range(h):
        row = rgb[y * w * 3:(y + 1) * w * 3]
        big = b"".join(row[i:i + 3] * k for i in range(0, len(row), 3))
        out += big * k
    return w * k, h * k, bytes(out)

d = sys.argv[1]
for p in sorted(glob.glob(os.path.join(d, "*.ppm"))):
    w, h, rgb = read_ppm(p)
    k = 3 if w == 240 else 2
    W, H, big = scale(w, h, rgb, k)
    write_png(p[:-4] + ".png", W, H, big)
    os.remove(p)
print(len(glob.glob(os.path.join(d, "*.png"))), "PNG files in", d)
