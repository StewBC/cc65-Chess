#!/usr/bin/env python3
"""Wrap a z88dk ZX binary as a 48K TAP the ROM loader will run.

zcc -create-app at ORG 23800 emits the image as a BASIC *program*
(a REM containing the code).  LOAD "" then prints "Program: chess"
and either sits there or RUNs line 10, which does not exist.
SmartLoad / drag-and-drop skip that and jump to the origin; the ROM
does not.

This writes CLEAR / LOAD "" CODE / RANDOMIZE USR, then a Bytes block
at the origin, which is what LOAD "" on a 48K expects.
"""

import argparse
import pathlib
import struct
import sys


def zx_int(n):
    """ASCII digits plus the 5-byte Spectrum integer form."""
    text = str(n).encode("ascii")
    return text + bytes((0x0E, 0x00, 0x00, n & 0xFF, (n >> 8) & 0xFF, 0x00))


def basic_line(number, payload):
    body = payload + b"\x0d"
    return struct.pack(">H", number) + struct.pack("<H", len(body)) + body


def tap_block(flag, payload):
    data = bytes((flag,)) + payload
    chk = 0
    for b in data:
        chk ^= b
    data += bytes((chk,))
    return struct.pack("<H", len(data)) + data


def header(kind, name, length, p1, p2):
    raw = (name.encode("ascii") + b" " * 10)[:10]
    return bytes((kind,)) + raw + struct.pack("<HHH", length, p1, p2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--org", type=int, default=24000)
    ap.add_argument("--name", default="chess")
    args = ap.parse_args()

    code = pathlib.Path(args.binary).read_bytes()
    org = args.org
    clear_at = org - 1
    # 48K PROG is ~23755.  CLEAR must sit above the loader plus a little
    # calculator stack or the ROM prints "M RAMTOP no good" at line 10.
    if clear_at < 23999:
        raise SystemExit(
            "org %d is too low for a 48K CLEAR (need org >= 24000)" % org
        )

    # 10 CLEAR org-1
    # 20 LOAD "" CODE
    # 30 RANDOMIZE USR org
    basic = b"".join(
        (
            basic_line(10, bytes((0xFD,)) + zx_int(clear_at)),
            basic_line(20, bytes((0xEF, 0x22, 0x22, 0xAF))),
            basic_line(30, bytes((0xF9, 0xC0)) + zx_int(org)),
        )
    )

    name = args.name
    tap = b"".join(
        (
            tap_block(0x00, header(0, name, len(basic), 10, len(basic))),
            tap_block(0xFF, basic),
            tap_block(0x00, header(3, name, len(code), org, 0x8000)),
            tap_block(0xFF, code),
        )
    )
    pathlib.Path(args.output).write_bytes(tap)
    print(
        "tap %s: BASIC %d + CODE %d at %d (%d bytes)"
        % (args.output, len(basic), len(code), org, len(tap))
    )


if __name__ == "__main__":
    sys.exit(main())
