#!/usr/bin/env python3
"""Write an EA5 program image from the two RAM blobs."""
import os
import struct
import sys


def read(path):
    with open(path, "rb") as f:
        return f.read()


def chunks(addr, data):
    while data:
        piece = data[:0x1FFA]
        data = data[0x1FFA:]
        yield addr, piece
        addr += len(piece)


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: ti99-ea5.py text.bin data.bin outfile\n")
        return 2
    text, data, out = sys.argv[1:]
    tex = read(text)
    dat = read(data)
    recs = list(chunks(0xA000, tex)) + list(chunks(0x2000, dat))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        for i, (addr, piece) in enumerate(recs):
            more = 0xFFFF if i < len(recs) - 1 else 0x0000
            f.write(struct.pack(">HHH", more, addr, 6 + len(piece)))
            f.write(piece)
    print("ti99 ea5: %d records, %d bytes -> %s" % (len(recs), os.path.getsize(out), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
