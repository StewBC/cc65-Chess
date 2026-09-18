# Agon Light (agondev)

`make agon` builds a MOS binary with the Agon C/C++ toolchain
([agondev](https://github.com/AgonPlatform/agondev)).  The product is
`build/agon/chess.bin`.  Copy it to the microSD card (or the emulator's
virtual one) and run `chess` from the MOS `*` prompt.

Light, Light 2 and Console8 share firmware, so one binary covers all three.
This machine has agondev at `/Volumes/EXTERNAL/ext-dev/agondev` and Fab Agon
Emulator at `~/.local/share/fab-agon-emulator-v1.2.5-macos-arm64`.

## Build

agondev's `agondev-config` must be on `PATH` (or set `AGONDEV_HOME` to the
toolchain prefix).  `make list` says `agon` is available when it is.

```bash
make agon
```

`-funsigned-char` is not optional: `PIECE_WHITE` is bit 7 of a `char`, and
clang's `char` is signed.  Nothing in shared `src/` is Agon-specific.

## Run

```bash
make agon test
```

That copies `chess.bin` to `$FAE_HOME/sdcard/bin/chess.bin`, writes
`autoexec.txt` so MOS starts the game on boot, and launches Fab Agon
Emulator.  The emulator must be started from its own directory — it loads
`firmware/*.so` relative to cwd.  `recipes/run-agon.sh` does that.

Standalone programs live in `/bin` and load at `$40000`.  Files in `/mos`
are moslets, loaded at `$B0000`.  This binary is the first kind; putting it
in `/mos` is a guru meditation at `$40046`.

`FAE_HOME` defaults to the install on this machine.  Override it if yours
lives elsewhere:

```bash
export FAE_HOME=/path/to/fab-agon-emulator-v1.2.5-macos-arm64
make agon test
```

On the real board, copy `build/agon/chess.bin` to the SD card's `bin`
folder and type `chess`.  Same keys as every other port.

## Display

VDP mode 12: 320×200, 64 colours, 40×25 characters.  C64 layout — 4×3
squares, 32×24 piece bitmaps, green field, log in the seven columns on the
right.  `B` / `A` / `D` are the same four numbers a square as the C64, with
colour to separate them.

The VDP is a command stream to the ESP32, not a framebuffer the eZ80 can
poke.  Squares are filled rectangles; pieces are RGBA bitmaps uploaded once
at init.

## Keys

Cursor keys, RETURN, ESC (backup / menu, in place of RUN/STOP), `M`, `B`,
`A`, `D`, `U`, `R`.  No new bindings.

Compiling is not running.  A clean `ld` only proves the binary linked.
