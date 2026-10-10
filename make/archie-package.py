#!/usr/bin/env python3
"""RISC OS !Chess application, HostFS ZIP and native Acorn-metadata ZIP.

Uses only the Python standard library.  The icon is the existing C64 king,
packed as a Mode 9 sprite with an explicit palette and an old-format mask.
"""
from datetime import datetime, timezone
from pathlib import Path
import re
import shutil
import struct
import sys
import zipfile


def icon(root):
    source = (root / 'src/c64/dataC64.c').read_text()
    values = [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{2}', source)]
    king = values[4 * 192 + 96:5 * 192]
    assert len(king) == 96
    pixels = bytearray(16 * 24)
    mask = bytearray(len(pixels))
    for y in range(24):
        for x in range(32):
            bit = king[(y // 8) * 32 + (x // 8) * 8 + y % 8] & (128 >> (x % 8))
            offset = y * 16 + x // 2
            shift = (x % 2) * 4
            if bit:
                pixels[offset] |= 1 << shift
                mask[offset] |= 15 << shift
    palette = b''.join(struct.pack('<II', c, c)
                       for c in [0, 0xffffff00] + [0] * 14)
    image_offset = 44 + len(palette)
    mask_offset = image_offset + len(pixels)
    size = mask_offset + len(mask)
    header = struct.pack('<I12s7I', size, b'!chess', 3, 23, 0, 31,
                         image_offset, mask_offset, 9)
    return struct.pack('<III', 1, 16, 16 + size) + header + palette + pixels + mask


def main():
    root = Path(__file__).resolve().parent.parent
    output = Path(sys.argv[1] if len(sys.argv) > 1 else root / 'build/archie')
    app = output / '!Chess'
    app.mkdir(parents=True, exist_ok=True)
    shutil.copy2(output / 'chess,ff8', app / '!RunImage,ff8')
    boot = 'Set Chess$Dir <Obey$Dir>\nIconSprites <Chess$Dir>.!Sprites\n'
    (app / '!Boot,feb').write_text(boot)
    (app / '!Run,feb').write_text(boot + 'WimpSlot -min 256K\n<Chess$Dir>.!RunImage\n')
    (app / '!Sprites,ff9').write_bytes(icon(root))
    (app / '!Help,fff').write_text(
        'cc65 Chess for Acorn A3010, 2 MB, RISC OS 3.11.\n'
        'Cursor keys move, Return selects. Mouse hover moves the cursor;\n'
        'left click selects a square or menu item.\n'
        'Escape cancels a selection or returns to the menu. M opens the menu.\n'
        'B shows all four square values; A/D show attackers/defenders.\n'
        'U/R undo/redo. The skill menu has the same four levels as other ports.\n'
        'Quit abandons an active game; Quit again exits to RISC OS.\n'
        'https://github.com/StewBC/cc65-Chess\n')
    shutil.copy2(root / 'LICENSE', app / 'Licence,fff')
    files = sorted(app.iterdir())
    for native in (False, True):
        path = output / ('Chess-RISCOS.zip' if native else 'Chess-HostFS.zip')
        with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
            for file in files:
                if not native:
                    archive.write(file, '!Chess/' + file.name)
                    continue
                name, file_type = file.name.rsplit(',', 1)
                timestamp = file.stat().st_mtime
                cs = int((timestamp + 2208988800) * 100)
                load = 0xfff00000 | (int(file_type, 16) << 8) | ((cs >> 32) & 255)
                info = zipfile.ZipInfo('!Chess/' + name,
                                      datetime.fromtimestamp(timestamp, timezone.utc).timetuple()[:6])
                info.compress_type = zipfile.ZIP_DEFLATED
                info.extra = struct.pack('<HH4sIIII', 0x4341, 20, b'ARC0',
                                         load, cs & 0xffffffff, 3, 0)
                archive.writestr(info, file.read_bytes())
        print(path)


if __name__ == '__main__':
    main()
