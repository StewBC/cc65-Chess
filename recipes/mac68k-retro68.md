# Macintosh 68k with Retro68

A `make` port on Retro68, not cc65.

## Environment

`m68k-apple-macos-gcc` is usually not on `PATH`, so `RETRO68` is how the
makefile finds it. On this machine:

```bash
export RETRO68=/Users/swessels/Develop/github/external/Retro68-build
```

It accepts either the toolchain prefix (`…/toolchain`) or the CMake build
tree that contains it — the path above is the build tree, and
`make/common.mk` appends `/toolchain` itself. `m68k-apple-macos-gcc` on
`PATH` works too; the makefile derives the prefix back from it. The default
when nothing is set is the relative `Retro68-build`, i.e. a build tree
sitting inside the repo, which is not how it is installed here.

Nothing else in the tree fails first — `make list` simply reports
`skipped — Retro68 not found (set RETRO68= or put m68k-apple-macos-gcc on
PATH)`, and a bare `make` builds every other port and exits 0 without the Mac. If the export lives in an
interactive shell profile only, anything non-interactive — an editor's build
task, CI, an agent shell — sees that skip and nothing else. Pass it on the
command line there:

```bash
RETRO68=/Users/swessels/Develop/github/external/Retro68-build make mac68k
```

## Build

```bash
make                        # includes mac68k when Retro68 is found
make mac68k
```

Output is gitignored, under `build/mac68k/`:

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

## Quit the emulator before rebuilding

`make mac68k` ends by reformatting `cc65-Chess.dsk` and copying the app back
onto it. While Basilisk has that image mounted it holds an exclusive lock on
the file, and the build stops with

```
hformat: .../build/mac68k/cc65-Chess.dsk: unable to obtain lock for medium
	(Resource temporarily unavailable)
make[1]: *** [build/mac68k/cc65-Chess.bin] Error 1
```

It reads like a broken toolchain and it is not — it is that one file being
held open. It also fails a bare `make`, which builds this port last, so every
other port succeeds and the run still exits non-zero. Eject the volume or
quit Basilisk and build again; nothing needs cleaning up afterwards.

## Run

Mount `cc65-Chess.dsk` Double-click `cc65-Chess` on that volume.

The `.bin` is what goes to a real 68k Mac. Mini vMac is not a target —
this needs System 7, and Mini vMac crashes.

## Keys

Same as every other port. Command-Q and the close box quit. Command-period
is Esc. Mouse-to-square is not wired yet; `square_rect` is the hit test.
