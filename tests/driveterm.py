#!/usr/bin/env python3
"""
driveterm.py - drive the curses build through a pty and read the screen back.

The unit tests cover the engine; this covers the bit they cannot, which is that
the game is actually playable - menus, cursor, the visualizer toggles, undo.

Build the target first:

  cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c \\
     src/search.c src/board.c src/undo.c src/cpu.c src/human.c \\
     src/frontend.c src/main.c src/term/platTerm.c -o /tmp/chessterm

Then, for example:

  python3 driveterm.py                      # start up and show the first screen
  python3 driveterm.py aivsai               # let the AI play itself for a bit
  python3 driveterm.py humanmove            # play e2-e4 as white, then undo

The ANSI model below is deliberately minimal - just enough to read a frame.
Partial redraws (plat_DrawSquare touches one square) can leave stale glyphs in
the model that a real terminal would not show, so read it as evidence that
things are being drawn, not as a pixel-accurate reference.
"""

import os, pty, time, sys, select, re, fcntl, termios, struct

GAME = os.environ.get("CHESSTERM", "/tmp/chessterm")
ROWS, COLS = 25, 80

CR, UP, DOWN = b'\r', b'\x1bOA', b'\x1bOB'
LEFT, RIGHT = b'\x1bOD', b'\x1bOC'


def run(script, settle=0.35):
    """script is a list of (keys, seconds-to-wait) pairs."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM='xterm', LINES=str(ROWS), COLUMNS=str(COLS))
        os.execv(GAME, [GAME])
        os._exit(1)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
    buf = b''

    def pump(seconds):
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    buf += os.read(fd, 65536)
                except OSError:
                    break

    pump(0.8)
    for keys, wait in script:
        os.write(fd, keys)
        pump(wait)

    try:
        os.write(fd, b'\x03')
    except OSError:
        pass
    time.sleep(0.2)
    os.kill(pid, 9)
    os.waitpid(pid, 0)
    return buf


def render(buf):
    scr = [[' '] * COLS for _ in range(ROWS)]
    r = c = i = 0
    data = buf.decode('utf8', 'replace')

    while i < len(data):
        ch = data[i]
        if ch == '\x1b':
            m = re.match(r'\x1b\[([0-9;]*)([A-Za-z])', data[i:])
            if m:
                p = [int(x) for x in m.group(1).split(';') if x]
                cmd = m.group(2)
                if cmd == 'H':
                    r = (p[0] - 1) if p else 0
                    c = (p[1] - 1) if len(p) > 1 else 0
                elif cmd == 'J' and (not p or p[0] == 2):
                    scr = [[' '] * COLS for _ in range(ROWS)]
                elif cmd == 'K':
                    for x in range(c, COLS):
                        scr[r][x] = ' '
                elif cmd in 'ABCD':
                    n = p[0] if p else 1
                    r += {'A': -n, 'B': n}.get(cmd, 0)
                    c += {'D': -n, 'C': n}.get(cmd, 0)
                i += m.end()
                continue
            i += 1
            continue

        if ch == '\n':
            r += 1
        elif ch == '\r':
            c = 0
        elif ch == '\b':
            c = max(0, c - 1)
        elif ord(ch) >= 32:
            if 0 <= r < ROWS and 0 <= c < COLS:
                scr[r][c] = ch
            c += 1
        i += 1

    return '\n'.join(''.join(row).rstrip() for row in scr)


# Menu layout: main menu item 1 is "1 Human player", 3 is "Both players AI".
# Choosing a mode leads to the colour menu (1 player only) and then the skill
# menu, both of which take RETURN for the first entry.
SCRIPTS = {
    'startup':    [(CR, 0.5)],
    'aivsai':     [(DOWN, 0.2), (DOWN, 0.2), (CR, 0.4), (CR, 4.0)],
    'attackview': [(CR, 0.3), (CR, 0.3), (CR, 0.6), (b'b', 0.6)],
    # cursor starts on e1; up to e2, select, up twice to e4, select, then undo
    'humanmove':  [(CR, 0.3), (CR, 0.3), (CR, 0.6),
                   (UP, 0.3), (CR, 0.3), (UP, 0.2), (UP, 0.3), (CR, 1.5),
                   (b'u', 1.0)],
}

if __name__ == '__main__':
    name = sys.argv[1] if len(sys.argv) > 1 else 'startup'
    if name not in SCRIPTS:
        sys.exit("scripts: " + ", ".join(SCRIPTS))
    print(render(run(SCRIPTS[name])))
