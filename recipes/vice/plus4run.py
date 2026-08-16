"""Start cc65-Chess.plus4 on a Plus/4 without using VICE's keyboard injection.

VICE's autostart feeds the LOAD/RUN commands through the Kernal keyboard buffer
($0527, count at $EF, 8 bytes).  On the Plus/4 that count underflows past zero
and the Kernal then drains ~255 bytes of the RAM that follows the buffer as if
they were keystrokes -- which is where the function-key macro strings live.  So
we write the program into RAM ourselves and hand BASIC a clean "RUN".

One connection for the whole run: closing the socket resumes emulation, so a
script that reconnects has already let the machine run unobserved.
"""
import os
import subprocess
import sys
import time

from vicebin import Vice, mem_set
from shot import shot

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
X = '/Applications/vice-arm64-gtk3-3.10/bin/xplus4'
PRG = os.path.join(ROOT, 'build', 'plus4', 'cc65-Chess')
ENTRY = 0x100D
PORT = 6502


def boot():
    subprocess.run(['pkill', '-f', 'xplus4 -TEDdsize'])
    time.sleep(2)
    subprocess.Popen([X, '-TEDdsize', '-binarymonitor',
                      '-binarymonitoraddress', 'ip4://127.0.0.1:%d' % PORT],
                     stdout=open('/dev/null', 'w'), stderr=subprocess.STDOUT)
    time.sleep(8)


def main():
    boot()
    data = open(PRG, 'rb').read()
    load = data[0] | (data[1] << 8)
    body = data[2:]
    end = load + len(body)
    print('prg loads at $%04X, %d bytes, ends $%04X' % (load, len(body), end))

    v = Vice(PORT, timeout=90)
    v.reg_names()
    print('booted: PC=$%04X NDX=%d' % (v.registers()['PC'],
                                       v.mem(0xEF, 0xEF, bank=1)[0]))

    for off in range(0, len(body), 2048):
        mem_set(v, load + off, body[off:off + 2048], bank=1)
    back = v.mem(load, load + 63, bank=1)
    print('written and verified:', back == body[:64])

    # BASIC end-of-program / start-of-variables pointers
    lo, hi = end & 0xFF, end >> 8
    for a in (0x2D, 0x2F, 0x31):
        mem_set(v, a, bytes([lo, hi]), bank=1)

    v.checkpoint(ENTRY, op=4)
    mem_set(v, 0x0527, b'RUN\r', bank=1)
    mem_set(v, 0xEF, bytes([4]), bank=1)
    v.cont()
    s = v.wait_stopped()
    print('reached entry $%04X (pc=$%04X)' % (ENTRY, s['pc']))

    for secs in (3, 8, 15):
        v.cont()
        time.sleep(secs)
        r = v.registers()
        name = 'plus4-t%02d.png' % secs
        print('t+%-3ds PC=$%04X  %s' % (secs, r['PC'], shot(v, name)))
    v.close()


if __name__ == '__main__':
    sys.exit(main())
