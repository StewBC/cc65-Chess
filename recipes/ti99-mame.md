# TI-99/4A with tms9900-gcc

A `make` port on tms9900-gcc, not cc65.  The chess is an EA5 image for a
32K machine.  MAME boots a paged378 cartridge that copies that image into
RAM and jumps to it — there is no Editor/Assembler dump here.

## Environment

```bash
export PATH="$HOME/.local/share/tms9900-gcc/bin:$PATH"
```

`make list` also finds that prefix if `tms9900-gcc` is not on PATH.
`-funsigned-char` is mandatory: `PIECE_WHITE` is bit 7 of a `char`.

Homebrew MAME 0.289 is `/opt/homebrew/bin/mame`.  Console ROMs live in
`~/.mame/roms`.  Ample.app's `mame64` has no `ti99_4a` driver.

## Build

```bash
make ti99
make ti99 test          # MAME, 32K PEB card, Lua jump into the cart
```

Output is gitignored, under `build/ti99/`:

| file | what |
|---|---|
| `chess.elf` | linked image, `.text` at `>A000`, `.data`/`.bss` at `>2000` |
| `chess.ea5` | EA5 program image (needs Editor/Assembler to load) |
| `chess.rpk` | paged378 cart — this is what `make ti99 test` boots |
| `chessC.bin` | raw cart ROM inside the rpk |
| `boot.lua` | jumps `WP=8300` / `PC` at cart `_start` |

32K is the PEB card, not the 16-bit console upgrade:

```
mame ti99_4a -rompath "$HOME/.mame/roms" \
  -cfg_directory "$HOME/.mame/cfg" \
  -ioport peb -ioport:peb:slot2 32kmem \
  -cart build/ti99/chess.rpk \
  -autoboot_script build/ti99/boot.lua \
  -window -nomaximize -skip_gameinfo
```

## Layout

32 columns × 24 rows, Graphics II.  Squares are 3×2 characters.  Files
along the top, ranks on the left, 7-column log, `Think` on the message
row.  Colours follow the C64: green field, white/black pieces, yellow
frame, yellow-on-blue menus.

## Keys

The machine's own keyboard, not PC arrows.  In MAME (emulated keyboard,
the default) FCTN is **Option** (either Alt).

| TI-99/4A | on the Mac in MAME | meaning |
|---|---|---|
| FCTN+E / S / D / X | Option+E / S / D / X | cursor |
| ENTER | Return | select |
| FCTN-9 | Option+9 | BACK (leave a menu) |
| M B A D U R | the letter keys | menu, board, attackers, defenders, undo, redo |

Do not press Option+= (FCTN-=).  **Quit Game** selects cart bank 0
(the header) then `BLWP @>0000`, which is the console title screen.
The cart is still plugged in, so CHESS is on that menu.

If keystrokes go to MAME instead of the game, tap **fn-Delete** once
(MAME's UI-mode key on a Mac).  Tab is the MAME menu; leave that with
Esc.

Arrow keys work if you add `-natural` (MAME maps them to FCTN+E/S/D/X).
Partial Keyboard Emulation is also in the Tab menu.

After the splash, Return.  Then the usual four-item skill menu: one
human, pick a colour, pick a skill, play.  `Think` on the right is the
engine; it is slow on a 3 MHz 9900 and that is expected.

## Size

Printed at link time from the map.  High RAM is 24K (`>A000–>FFFF`)
with the stack at `>FFFC`.  Low RAM is 8K (`>2000–>3FFF`) for `.data`
and `.bss`.  `SEARCH_ARENA` is 256 in `make/ports/ti99.mk`.
