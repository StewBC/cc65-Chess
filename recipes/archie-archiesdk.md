# Acorn Archimedes / ArchieSDK

Target: A3010, ARM250, 2 MB RAM, RISC OS 3.11. Development testing uses Arculator
v2.2's A3010 with 4 MB, as TimePilot-Archie did. No optional RISC OS modules are needed.

## Build and transfer

```sh
export ARCHIESDK=/Users/swessels/Develop/gitlab/archiesdk
make archie
```

ArchieSDK's GCC 8.5.0 builds the shared engine with `-Os`, `-funsigned-char`, ARM2
instructions and software floating point. `PLAT_CURSOR_JUMP` enables the existing
mouse hook. `plat.h`, the engine and the other platform files are unchanged.

Products under `build/archie/`:

| Product | Use |
| --- | --- |
| `chess,ff8` | Standalone RISC OS absolute executable |
| `!Chess/` | Double-clickable application for emulator HostFS |
| `Chess-HostFS.zip` | Host extraction, with comma file-type suffixes |
| `Chess-RISCOS.zip` | Extract on RISC OS; Acorn ZIP metadata preserves file types |
| `chess.elf`, `chess.map` | Symbols and memory layout for development |

For hardware, transfer `Chess-RISCOS.zip` intact and extract it on RISC OS with
SparkPlug or another extractor that preserves Acorn metadata. Set the ZIP's file
type to Archive (&DDC) if it was lost in transit, then double-click `!Chess`.
The launcher allocates a 256 KB Wimp slot. The SDK has a 64 KB static stack inside
the executable; the game, that stack and BSS (including the 40 KB repaint buffer)
fit within the slot. Screen memory
is a separate RISC OS allocation (40 KB for Mode 9).

The package script uses Python's standard library. The Filer icon comes from the
existing C64 king bitmap; no new asset converter or runtime resource files are needed.

## Arculator

```sh
make archie test
```

`recipes/run-archie.sh` copies the emulator executable, configuration and CMOS into
`build/archie/arculator/`, links its read-only ROM/library/podule assets, and copies
`!Chess` into the private HostFS root. The installed emulator's configuration and
HostFS are untouched. It uses the existing `A3010.cfg`; either 2 or 4 MB works.
The default installation is `$HOME/Applications/arculator`; set `ARCU_HOME` for another.
On macOS the script launches a private `Chess Arculator.app` bundle so the emulator
can receive normal application focus and keyboard input.

Open the HostFS icon and double-click `!Chess`. Arculator's first click captures
the host mouse; Ctrl-End releases it. The game itself occupies the guest screen;
the emulator can stay in a host window.

For command-line launch inside the guest, F12 opens the CLI:

```text
WimpSlot -min 256K
Run HostFS:$.!Chess.!RunImage
```

On exit from a CLI launch, press Return to restore the desktop. RISC OS typing via
automation needs physical key presses; pasting host text does not feed the emulated
keyboard. Hold individual test keys for about 100–200 ms so the guest sees them.

## Presentation and controls

Mode 9 is 320×256 with 16 programmable colours. A 320×200 C64 layout is centered
vertically with 28 green rows on either side. The 32×24 C64 pieces retain their
solid/outline choice and square-dependent inversion; labels, menus, log and messages
use an existing public-domain 8×8 font. The VIDC rounds the C64 RGB palette to four
bits per component. Palette and mode details follow the
[RISC OS VDU reference](https://www.riscos.com/support/developers/prm/vdu.html).

- Cursor keys move; Return selects. Held arrows repeat; other keys act once per press.
- Mouse movement changes the board cursor; left click selects a square or menu item.
  Right click acts as Escape: cancel, back, or open the menu.
  Mouse movement takes effect when the pointer moves, so a stationary mouse does not
  pull the cursor back after keyboard navigation.
- Escape cancels a piece selection, backs out of a menu or opens the menu, as RUN/STOP
  does on the C64. `M` opens the menu. Either also interrupts AI thinking.
- `B` shows attackers bottom left, defenders bottom right, piece type top left and
  colour top right. `A` and `D` retain their attacker/defender highlights.
- `U` and `R` undo/redo. The same four difficulty levels and node budgets are used.
- Quit abandons an active game; Quit from the new-game menu exits to RISC OS.

The move log asks the undo ring at draw time. Board/menu repaints preserve the log
globals around their own ring walk. Full board repaints are composed in a 40 KB
software buffer and copied to the visible screen after VSync, keeping the coordinate
labels visible during undo, redo and menu removal. This is not hardware page flipping;
individual square and menu updates still draw directly to the screen.
Changing the menu selection redraws only the old and new selected rows, without
blanking them first. Other menu items stay untouched; the scroller updates separately.
Shutdown restores the prior screen mode and pointer selection, clears Escape and
discards queued gameplay characters.

## Verification

```sh
make -C tests test
```

The native suite includes `archiecheck`, which exercises framebuffer guard bytes,
repaint preservation of the seven log globals and coordinate labels, an unchanged
visible framebuffer until presentation, mouse jump-before-select, held buttons,
clicks outside the board, nested menu clicks, keyboard edges/repeat and idempotent
cleanup and right-click cancellation in menus and during search. These checks use
SWI doubles; they do not validate ARM instructions or replace
emulator play.

Arculator checks during the port: splash and mouse menu selection, two-human play,
`e2-e4`, undo/redo and its log entry, the `B/A/D` displays, AI play and Escape during
thinking, and quitting/restarting the executable. The available 6502 ports also build.

All four skill levels were exercised in the ARM executable. Very Hard completed
search moves; Very Easy also played through both sides castling, which checked the
full-board redraw and log reconstruction. `M` and Escape both interrupted AI play.
The user subsequently confirmed the buffered board repaint visibly improved menu
removal and coordinate-label flicker. The later two-row menu selection redraw has
been built and checked with the native platform suite; its visual result still needs
confirmation. Real hardware and a 2 MB emulator configuration remain untested.
