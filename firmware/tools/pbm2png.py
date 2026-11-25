#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Convert a PBM from preview_face into a PNG that looks like the reflective panel.

Standard library only (no Pillow), so it runs anywhere Python 3 does.

    python3 tools/pbm2png.py face.pbm face.png [scale]   (scale defaults to 2)
"""
import struct
import sys
import zlib

INK = (34, 34, 34)
PAPER = (226, 228, 220)  # the panel's grey-green background
EDGE = (120, 120, 120)
BORDER = 4


def read_pbm(path):
    data = open(path, "rb").read()
    toks, i = [], 0
    while len(toks) < 3:
        while data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while data[i : i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not data[j : j + 1].isspace():
            j += 1
        toks.append(data[i:j])
        i = j
    i += 1
    w, h = int(toks[1]), int(toks[2])
    if toks[0] == b"P1":
        bits = [b - 48 for b in data[i:] if b in (48, 49)]
        return w, h, [bits[y * w : (y + 1) * w] for y in range(h)]
    if toks[0] == b"P4":
        rb = (w + 7) // 8
        return w, h, [[(data[i + y * rb + x // 8] >> (7 - x % 8)) & 1 for x in range(w)] for y in range(h)]
    sys.exit(f"{path}: not a PBM (P1/P4)")


def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)


def write_png(path, w, h, rows, scale):
    W, H = w * scale + 2 * BORDER, h * scale + 2 * BORDER
    raw = bytearray()
    for Y in range(H):
        raw.append(0)  # filter type: none
        for X in range(W):
            if Y < BORDER or Y >= H - BORDER or X < BORDER or X >= W - BORDER:
                raw += bytes(EDGE)
            else:
                raw += bytes(INK if rows[(Y - BORDER) // scale][(X - BORDER) // scale] else PAPER)
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    open(path, "wb").write(png + chunk(b"IEND", b""))


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    w, h, rows = read_pbm(sys.argv[1])
    write_png(sys.argv[2], w, h, rows, int(sys.argv[3]) if len(sys.argv) == 4 else 2)


if __name__ == "__main__":
    main()
