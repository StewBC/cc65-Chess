# Macintosh 68k with Retro68

A `make` port on Retro68, not cc65. `m68k-apple-macos-gcc` is usually not
on `PATH`. The makefile accepts `RETRO68` as either the toolchain prefix
(`…/toolchain`) or the CMake build tree that contains it

```bash
make mac68k
```

Not a default port. Output is gitignored, under `build/mac68k/`:

| file | what |
|---|---|
| `cc65-Chess.bin` | MacBinary — FTP this to a real Mac and drop the `.bin` |
| `cc65-Chess.dsk` | 800K HFS disk image, volume `cc65-Chess` |
| `cc65-Chess.APPL` | application; data fork is empty, the code is in the resource fork |
| `cc65-Chess.code.bin` | ELF/code that Rez copies into the app |

Creator is `Cchs`. 68000 codegen, System 7. Color QuickDraw if the machine
has it, otherwise the eight basic QD colors. Pieces are the Frank Gebhart
silhouettes the C64 uses, scaled to 32×32. The Finder icon is the knight
(`ICN#` 32 and `ics#` 16), and the build sets the bundle bit so Finder
reads the `BNDL`. Retro68's Rez does not do that on its own.

Mouse: move to change the menu row or the board cursor, click for
Return.  The highlight is the truth; a click does not pick a different
row or square.  Keyboard and mouse hand off without fighting.

The file also carries a custom icon (`ICN#` −16455).  That is the Get Info
paste, and it does not go through the desktop database.

Basilisk on a modern Mac often never sees Command-Option at boot — the
host eats it — so the desktop cache is not rebuilt.  Other ways, from
inside the emulated Finder:

- System 7.5+: hold Option and choose Special → Rebuild Desktop.
- Throw away `Desktop`, `Desktop DB` and `Desktop DF` in the System
  Folder (and on the chess floppy if they are there), then restart.
- Copy `cc65-Chess` onto the hard disk; a first copy with the bundle bit
  set is what registers a new creator.

## Run

Mount `cc65-Chess.dsk` Double-click `cc65-Chess` on that volume.

The `.bin` is what goes to a real 68k Mac. Mini vMac is not a target —
this needs System 7, and Mini vMac crashes.

## Keys

Same as every other port. Command-Q and the close box quit. Command-period
is Esc. Mouse-to-square is not wired yet; `square_rect` is the hit test.
