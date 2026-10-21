#!/usr/bin/env python3
"""Generate res/app.ico (PNG-compressed, 256/48/32/16) with no third-party dependencies."""
import math, struct, zlib, sys, os

def sdf(px, py, x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    dx = abs(px - cx) - ((x1 - x0) / 2 - r)
    dy = abs(py - cy) - ((y1 - y0) / 2 - r)
    return math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - r

def hexc(h): return tuple(int(h[i:i + 2], 16) / 255 for i in (1, 3, 5))

# (x0, y0, x1, y1, radius, colour | callable(y)->colour)   -- 256-unit design space
def lerp(a, b, t): return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))
TOP, BOT = hexc("#3B8BEB"), hexc("#1B4FA8")
SHAPES = [
    (8, 8, 248, 248, 52, lambda y: lerp(TOP, BOT, (y - 8) / 240)),
    (52, 56, 204, 200, 16, hexc("#FFFFFF")),
    (60, 66, 196, 100, 9, hexc("#CFE2FB")),
    (72, 75, 88, 91, 4, hexc("#2F7DE1")),
    (72, 115, 88, 131, 4, hexc("#F29A4A")),
    (72, 155, 88, 171, 4, hexc("#3FB56B")),
    (100, 78, 176, 88, 5, hexc("#1B4FA8")),
    (100, 118, 188, 128, 5, hexc("#6B7B8F")),
    (100, 158, 160, 168, 5, hexc("#6B7B8F")),
]

def render(size, ss=3):
    k = 256 / size
    rows = []
    for j in range(size):
        row = bytearray()
        for i in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sj in range(ss):
                for si in range(ss):
                    x = (i + (si + .5) / ss) * k
                    y = (j + (sj + .5) / ss) * k
                    r = g = b = a = 0.0
                    for x0, y0, x1, y1, rad, col in SHAPES:
                        cov = min(max(0.5 - sdf(x, y, x0, y0, x1, y1, rad) / k, 0), 1)
                        if cov <= 0: continue
                        c = col(y) if callable(col) else col
                        r = c[0] * cov + r * (1 - cov); g = c[1] * cov + g * (1 - cov)
                        b = c[2] * cov + b * (1 - cov); a = cov + a * (1 - cov)
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
            n = ss * ss
            a = acc[3] / n
            if a > 0: row += bytes(int(min(255, acc[t] / n / a * 255 + .5)) for t in range(3)) + bytes([int(a * 255 + .5)])
            else: row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows

def png(size, rows):
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")

sizes = [256, 48, 32, 16]
imgs = [png(s, render(s)) for s in sizes]
out = struct.pack("<HHH", 0, 1, len(sizes))
off = 6 + 16 * len(sizes)
for s, d in zip(sizes, imgs):
    out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(d), off); off += len(d)
out += b"".join(imgs)
dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "res", "app.ico")
open(dest, "wb").write(out)
open("/tmp/icon256.png", "wb").write(imgs[0])
print("wrote", dest, len(out), "bytes")
