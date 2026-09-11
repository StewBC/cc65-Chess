# Color Computer 3 with CMOC

A `make` port on CMOC, not cc65.  The chess is a Disk BASIC `.bin` that
LOADM/EXECs into the 32K mapped at boot.  GIME 320×200×16 lives in MMU
blocks `$30–$33`, paged through an 8K window at `$8000`.  128K is enough.

## Environment

```bash
export PATH="$HOME/.local/share/cmaoc-0.1.100/bin:$HOME/.local/share/lwtools/usr/bin:$PATH"
```

`make list` also finds that CMOC prefix if `cmoc` is not on PATH.
`-funsigned-char` is mandatory: `PIECE_WHITE` is bit 7 of a `char`.

XRoar 1.12 is `$HOME/.local/share/xroar-1.12-macosx/XRoar.app`.  CoCo 3
ROMs live in `~/Library/XRoar/roms` (`coco3.rom`, `disk11.rom`).  Homebrew
MAME 0.289 can boot the same `.dsk` from `~/.mame/roms`.

## Build

```bash
make coco3
make coco3 test          # XRoar, -run the DECB binary
```

Output is gitignored, under `build/coco3/`:

| file | what |
|---|---|
| `chess.bin` | Disk BASIC LOADM image, org `$0E00` |
| `chess.dsk` | 35-track RS-DOS disk with `CHESS.BIN` |

XRoar:

```
xroar -machine coco3 -rompath "$HOME/Library/XRoar/roms" \
  -run build/coco3/chess.bin
```

On a real CoCo 3: `LOADM"CHESS":EXEC`.  128K, RGB by default.

## Layout

40 columns × 25 rows, GIME 320×200×16.  Squares are 4×3 characters, the
C64 size.  Files along the top, ranks on the left, 7-column log.  Colours
follow the C64: green field, white/black pieces on light/dark grey,
yellow frame, yellow-on-blue menus.

RGB palette is the default.  Hold **C** on the splash (before ENTER) for composite; that is not shown on the screen.

Quit from the main menu cold-starts BASIC (the GIME takeover cannot return to `OK` any other way).

## Keys

The machine's own keyboard.

| CoCo 3 | meaning |
|---|---|
| arrows | cursor |
| ENTER | select |
| BREAK | backup (leave a menu) |
| M B A D U R | menu, board, attackers, defenders, undo, redo |

After the splash, ENTER.  Then the usual four-item skill menu.

## Size

Printed at link time from the DECB header.  Code+data must fit
`$0E00–$7E00`.  The framebuffer is not in that map.
