#!/usr/bin/env python3
"""Scale the Frank Gebhart 21x24 piece art (from src/c64/genpieces.cpp)
to 32x32 Mac 1-bit bitmaps and a Finder ICN# / ics#.

    python3 src/mac68k/genpieces.py
"""
from __future__ import print_function

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC_CPP = os.path.join(ROOT, "src", "c64", "genpieces.cpp")
OUT_C = os.path.join(ROOT, "src", "mac68k", "dataMac68k.c")
OUT_H = os.path.join(ROOT, "src", "mac68k", "dataMac68k.h")
OUT_ICON = os.path.join(ROOT, "src", "mac68k", "icon.r")
PREVIEW = os.path.join(ROOT, "src", "mac68k", "pieces-preview.pbm")

SRC_W, SRC_H = 21, 24
DST = 32
NAMES = ("rook", "knight", "bishop", "queen", "king", "pawn")


def parse_ascii(path):
    text = open(path).read()
    m = re.search(r'char pieces\[\]\s*=\s*(.*?);', text, re.S)
    if not m:
        sys.exit("pieces[] not found in %s" % path)
    rows = re.findall(r'"([^"]*)"', m.group(1))
    # 6 pieces x 2 versions x 24 rows
    expect = 6 * 2 * SRC_H
    if len(rows) != expect:
        sys.exit("expected %d rows, got %d" % (expect, len(rows)))
    art = []
    i = 0
    for p in range(6):
        versions = []
        for v in range(2):
            grid = []
            for _ in range(SRC_H):
                row = rows[i]
                i += 1
                if len(row) < SRC_W:
                    row = row.ljust(SRC_W)
                grid.append([1 if c == "*" else 0 for c in row[:SRC_W]])
            versions.append(grid)
        art.append(versions)
    return art


def scale(grid, dw, dh):
    sh = len(grid)
    sw = len(grid[0])
    out = []
    for y in range(dh):
        sy = y * sh // dh
        row = []
        for x in range(dw):
            sx = x * sw // dw
            row.append(grid[sy][sx])
        out.append(row)
    return out


def pad_center(grid, dw, dh):
    sh = len(grid)
    sw = len(grid[0])
    ox = (dw - sw) // 2
    oy = (dh - sh) // 2
    out = [[0] * dw for _ in range(dh)]
    for y in range(sh):
        for x in range(sw):
            out[y + oy][x + ox] = grid[y][x]
    return out


def to_bytes(grid):
    """Mac 1-bit, width rounded up to a byte, MSB left."""
    w = len(grid[0])
    bw = (w + 7) // 8
    data = []
    for row in grid:
        for bx in range(bw):
            b = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < len(row) and row[x]:
                    b |= 0x80 >> bit
            data.append(b)
    return bytes(data)


def c_array(data, indent="\t\t"):
    lines = []
    for i in range(0, len(data), 8):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + 8])
        lines.append(indent + chunk + ",")
    return "\n".join(lines)


def rez_hex(data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexed = " ".join(
            "".join("%02X" % chunk[j + k] for k in range(min(2, len(chunk) - j)))
            for j in range(0, len(chunk), 2)
        )
        lines.append('\t$"%s"' % hexed)
    return "\n".join(lines)


def checker(size, cell):
    g = [[0] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            if ((x // cell) ^ (y // cell)) & 1:
                g[y][x] = 1
    return g


def or_grid(dst, src, ox, oy):
    for y, row in enumerate(src):
        for x, p in enumerate(row):
            if p and 0 <= y + oy < len(dst) and 0 <= x + ox < len(dst[0]):
                dst[y + oy][x + ox] = 1


def frame(grid, t=1):
    h, w = len(grid), len(grid[0])
    for i in range(w):
        for k in range(t):
            grid[k][i] = 1
            grid[h - 1 - k][i] = 1
    for i in range(h):
        for k in range(t):
            grid[i][k] = 1
            grid[i][w - 1 - k] = 1


def filled_mask(size):
    return [[1] * size for _ in range(size)]


def write_pbm(path, tiles):
    """tiles: list of (name, 32x32 grid). 3 columns."""
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    W, H = 32 * cols, 32 * rows
    pix = [[0] * W for _ in range(H)]
    for i, (_n, g) in enumerate(tiles):
        r, c = divmod(i, cols)
        for y in range(32):
            for x in range(32):
                pix[r * 32 + y][c * 32 + x] = g[y][x]
    with open(path, "w") as f:
        f.write("P1\n%d %d\n" % (W, H))
        for y in range(H):
            f.write(" ".join(str(p) for p in pix[y]) + "\n")


def main():
    art = parse_ascii(SRC_CPP)
    scaled = []
    for p in range(6):
        vers = []
        for v in range(2):
            # 21x24 -> 28x32 (same 4/3), then centre in 32x32
            g = scale(art[p][v], 28, 32)
            g = pad_center(g, DST, DST)
            vers.append(g)
        scaled.append(vers)

    with open(OUT_H, "w") as f:
        f.write("/*\n *\tdataMac68k.h\n *\tcc65 Chess — Macintosh 68k piece bitmaps\n"
                " *\n *\t32x32 1-bit, 4 bytes a row.  [piece-1][0]=outline [1]=solid.\n"
                " *\tFrank Gebhart designs, scaled from src/c64/genpieces.cpp.\n */\n\n"
                "#ifndef _DATAMAC68K_H_\n#define _DATAMAC68K_H_\n\n"
                "extern const unsigned char gfxTiles[6][2][128];\n\n"
                "#endif\n")

    with open(OUT_C, "w") as f:
        f.write("/*\n *\tdataMac68k.c\n *\tcc65 Chess — Macintosh 68k piece bitmaps\n"
                " *\n *\tgenerated by src/mac68k/genpieces.py — do not hand-edit.\n"
                " *\tFrank Gebhart, 1980s; same art the C64 port uses.\n */\n\n"
                "#include \"dataMac68k.h\"\n\n"
                "const unsigned char gfxTiles[6][2][128] =\n{\n")
        for p, name in enumerate(NAMES):
            f.write("\t/* %s */\n\t{\n" % name)
            for v, label in enumerate(("outline", "solid")):
                f.write("\t\t/* %s */\n\t\t{\n" % label)
                f.write(c_array(to_bytes(scaled[p][v]), "\t\t\t") + "\n")
                f.write("\t\t},\n")
            f.write("\t},\n")
        f.write("};\n")

    # Finder icon: white field, framed knight.  no board under it —
    # that ate the silhouette at 16 pixels.
    icon = [[0] * 32 for _ in range(32)]
    knight = scale(art[1][1], 26, 28)
    or_grid(icon, knight, 3, 2)
    frame(icon, 2)
    mask = filled_mask(32)

    small = [[0] * 16 for _ in range(16)]
    kn16 = scale(art[1][1], 12, 14)
    or_grid(small, kn16, 2, 1)
    frame(small, 1)
    mask16 = filled_mask(16)

    icn = rez_hex(to_bytes(icon) + to_bytes(mask))
    ics = rez_hex(to_bytes(small) + to_bytes(mask16))
    with open(OUT_ICON, "w") as f:
        f.write("/* generated by genpieces.py — Finder ICN# (32) and ics# (16)\n"
                " * 128 is the BNDL local ID; -16455 is the per-file custom icon\n"
                " * so Finder does not need the desktop database.\n */\n\n")
        f.write("data 'ICN#' (128, purgeable) {\n%s\n};\n\n" % icn)
        f.write("data 'ics#' (128, purgeable) {\n%s\n};\n\n" % ics)
        f.write("data 'ICN#' (-16455, purgeable) {\n%s\n};\n\n" % icn)
        f.write("data 'ics#' (-16455, purgeable) {\n%s\n};\n" % ics)

    preview = []
    for p, name in enumerate(NAMES):
        preview.append(("%s-outline" % name, scaled[p][0]))
        preview.append(("%s-solid" % name, scaled[p][1]))
    preview.append(("icon32", icon))
    preview.append(("icon16", pad_center(small, 32, 32)))
    write_pbm(PREVIEW, preview)
    print("wrote", OUT_C)
    print("wrote", OUT_H)
    print("wrote", OUT_ICON)
    print("wrote", PREVIEW)


if __name__ == "__main__":
    main()
