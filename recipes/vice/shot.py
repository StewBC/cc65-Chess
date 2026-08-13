import re
import struct
import zlib

PAL = {}
for _l in open('/Applications/vice-arm64-gtk3-3.10/VICE.app/Contents/'
               'Resources/share/vice/PLUS4/yape-pal.vpl'):
    _m = re.match(r'^\s*([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})', _l)
    if _m and not _l.strip().startswith('#'):
        PAL[len(PAL)] = bytes(int(g, 16) for g in _m.groups())


def _chunk(t, d):
    return (struct.pack('>I', len(d)) + t + d
            + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff))


def shot(v, path):
    r = v.cmd(0x84, bytes([0, 0]))
    b = r['body']
    hl = struct.unpack('<I', b[:4])[0]
    dw, dh = struct.unpack('<HH', b[4:8])
    img = b[4 + hl:]
    rows = [b'\x00' + b''.join(PAL.get(img[y * dw + x], b'\xff\x00\xff')
                               for x in range(dw)) for y in range(dh)]
    png = (b'\x89PNG\r\n\x1a\n'
           + _chunk(b'IHDR', struct.pack('>IIBBBBB', dw, dh, 8, 2, 0, 0, 0))
           + _chunk(b'IDAT', zlib.compress(b''.join(rows)))
           + _chunk(b'IEND', b''))
    open(path, 'wb').write(png)
    return dw, dh, len(set(img))
