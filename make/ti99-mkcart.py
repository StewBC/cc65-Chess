#!/usr/bin/env python3
"""Pack the TI cart loader + EA5 payload into a paged378 RPK."""
import os
import struct
import sys
import zipfile


def read(path):
    with open(path, "rb") as f:
        return f.read()


def main():
    if len(sys.argv) != 5:
        sys.stderr.write("usage: ti99-mkcart.py header.bin text.bin data.bin outdir\n")
        return 2
    header, text, data, outdir = sys.argv[1:]
    hdr = bytearray(read(header))
    tex = read(text)
    dat = read(data)
    if len(tex) & 1:
        tex += b"\x00"
    if len(dat) & 1:
        dat += b"\x00"

    mark = hdr.find(b"CHSS")
    if mark < 0:
        sys.stderr.write("CHSS marker missing from header.bin\n")
        return 1
    # marker, then four words: text dest, text size, data dest, data size
    struct.pack_into(">HHHH", hdr, mark + 4, 0xA000, len(tex), 0x2000, len(dat))

    bank0 = bytearray(8192)
    if len(hdr) > 8192:
        sys.stderr.write("header.bin larger than 8K\n")
        return 1
    bank0[: len(hdr)] = hdr

    payload = tex + dat
    rom = bytes(bank0) + payload
    n = 8192
    while n < len(rom):
        n *= 2
    rom = rom + bytes(n - len(rom))

    os.makedirs(outdir, exist_ok=True)
    bin_path = os.path.join(outdir, "chessC.bin")
    rpk_path = os.path.join(outdir, "chess.rpk")
    xml_path = os.path.join(outdir, "layout.xml")
    with open(bin_path, "wb") as f:
        f.write(rom)
    xml = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<romset version=\"1.0\">\n"
        "   <resources>\n"
        '      <rom id="romimage" file="chessC.bin"/>\n'
        "   </resources>\n"
        "   <configuration>\n"
        '      <pcb type="paged378">\n'
        '         <socket id="rom_socket" uses="romimage"/>\n'
        "      </pcb>\n"
        "   </configuration>\n"
        "</romset>\n"
    )
    with open(xml_path, "w") as f:
        f.write(xml)
    with zipfile.ZipFile(rpk_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("layout.xml", xml)
        z.write(bin_path, "chessC.bin")

    plist = struct.unpack_from(">H", hdr, 6)[0]
    entry = struct.unpack_from(">H", hdr, (plist - 0x6000) + 2)[0]
    lua = os.path.join(outdir, "boot.lua")
    with open(lua, "w") as f:
        f.write(
            "local jumped = false\n"
            "local frames = 0\n"
            "emu.register_frame_done(function()\n"
            "\tif jumped then return end\n"
            "\tframes = frames + 1\n"
            "\tif frames < 8 then return end\n"
            "\tlocal cpu = manager.machine.devices[\":maincpu\"]\n"
            "\tcpu.state[\"WP\"].value = 0x8300\n"
            "\tcpu.state[\"PC\"].value = 0x%04X\n"
            "\tjumped = true\n"
            "end)\n" % entry
        )
    print(
        "ti99 cart: header %d  text %d  data %d  rom %d  entry >%04X -> %s"
        % (len(hdr), len(tex), len(dat), len(rom), entry, rpk_path)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
