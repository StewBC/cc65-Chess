# ZX Spectrum with z88dk

A `make` port on the z88dk toolchain, not cc65.

## Environment

Two variables, and on this machine they are:

```bash
export PATH="$PATH:/Users/swessels/Downloads/z88dk/bin"
export ZCCCFG=/Users/swessels/Downloads/z88dk/lib/config
```

`PATH` is what `make list` tests, so without it the port reports
`skipped — zcc not on PATH` and a bare `make` leaves the Spectrum out.

**`ZCCCFG` fails much later than you would expect, and the message points at
the wrong thing.** `zcc` compiles and links the whole program without it —
`build/spectrum/chess` and `chess.tap` both appear — and then the `.sna` step
stops with

```
zx: Error: SNA prototype src/appmake/data/zx_48.sna not found
```

which reads like a broken z88dk install or a bad source change. The file is
there; `z88dk-appmake` just resolves its own data directory relative to the
root it derives from `ZCCCFG`, and with the variable unset it looks under the
current directory. Set it and the same build finishes.

If these live in an interactive shell profile only, anything non-interactive —
an editor's build task, CI, an agent shell — will not see them. Pass them on
the command line there:

```bash
ZCCCFG=/Users/swessels/Downloads/z88dk/lib/config make spectrum
```

`src/spectrum/platSpectrum.c` is a real `plat.h`: board, files and ranks,
side to move, the move log, menus, B/A/D, and a flashing `Think`. Pieces are
24×16 silhouettes in `gfxTiles` — hand-edit those later if you want.

## Build

```bash
make spectrum
make spectrum test          # ZEsarUX, via run-spectrum.sh
```

Output is gitignored, under `build/spectrum/`:

| file | what |
|---|---|
| `chess` | raw memory image, loaded at 24000 (`$5DC0`) |
| `chess.tap` | BASIC loader (`CLEAR 23999` / `LOAD "" CODE` / `USR 24000`) plus that image |
| `chess.sna` | 48K snapshot — this is what `make spectrum test` boots |
| `chess.map` | z88dk map; the build prints the useful numbers from it |

ORG is 24000 and `REGISTER_SP` is `$FF57`. 23800 left no room above the
BASIC loader, so `CLEAR 23799` printed `M RAMTOP no good`. 25000 started
but the search walked onto the undo ring.

## Size

| | bytes |
|---|---|
| code | ~29,300 |
| rodata | 3,734 |
| data | 237 |
| bss | 4,328 |
| **total RAM from `$5DC0`** | **~37,830** |
| stack/heap to `$FF56` | ~3,500 |

A 48K Spectrum has about 41K above the display file and the system variables.
This still fits, with a couple of kilobytes of stack.

## Layout

32 columns × 24 rows. Squares are 3×2 characters (24×16 pixels):

```
  A  B  C  D  E  F  G  H  White
8 ...board...              e2-e4+
7                          ...
1
                           Think
```

Light squares are blue paper, dark squares green. Black pieces are black ink,
white pieces bright white — both stay visible. Highlight is the C64's two
vertical bars, as attribute changes on the left and right columns. `Think`
uses the ULA FLASH bit.

## Keys

Same meanings as every other port. 48K has no dedicated arrows; they are
Caps Shift plus 5/6/7/8 (and the 128K cursor keys close the same lines).

| key | meaning |
|---|---|
| Caps+7 / ↑ | up |
| Caps+6 / ↓ | down |
| Caps+5 / ← | left |
| Caps+8 / → | right |
| Enter | select |
| Space (BREAK) | back / menu |
| M | menu |
| B | attack board |
| A | attackers |
| D | defenders |
| U | undo |
| R | redo |

`plat_GetSeed` reads `FRAMES` at 23672.

## Loading the tap

From the repo root, after a build:

```bash
make spectrum test
# or
recipes/run-spectrum.sh
```

That calls the ZEsarUX **binary** with an absolute `--tape` path. Do not
use `open -a ZEsarUX chess.tap`: on this Mac, `.tap` is claimed by
RetroArch ("RetroArch Content File") and Launch Services puts up that
warning. Dropping the file on an already-open ZEsarUX *window* is fine —
the running app takes the path itself.

The binary `cd`s into `Contents/MacOS`, so a repo-relative tap path will
not be found. The script already passes an absolute path.

`run-spectrum.sh` boots `build/spectrum/chess.sna` and skips the tape
entirely. Drop that `.sna` on an open ZEsarUX window if you prefer.

The tap is for `LOAD ""`: `Program: chess`, then `Bytes: chess`, then the
splash. `j` is keyword `LOAD`, `"` is Option-P.

Remote protocol (port 10000) is `--enable-remoteprotocol`. `save-screen`
from ZRCP writes a black BMP with the cocoa driver; dump `$4000`–`$5AFF`
instead if you want a screenshot.
