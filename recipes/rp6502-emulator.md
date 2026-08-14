# Running the Picocomputer build

`rp6502-emu` is the reason `rp6502` is the easiest target here to be sure about. It boots a
ROM headless, takes a keyboard script, and exits non-zero when a check fails — so "does it
still play" is a command, not an afternoon.

## Building the ROM

```bash
make OPTIONS=optsize TARGETS=rp6502 && make rom
```

Two invocations, for the reason `AGENTS.md` gives: `TARGETS=` puts the suffix on `$(PROGRAM)`,
so `make TARGETS=rp6502 rom` asks for `cc65-Chess.rp6502.rp6502` and a bare `make rom` has no
rule for the binary it needs. The output is `cc65-Chess-rp6502.rp6502`, which is not a ROM in
the old sense — it is a file holding a memory image the RIA copies into RAM before it lets the
6502 out of reset.

## The emulator

Built alongside the firmware, at `../rp6502/build/emulator/release/rp6502-emu`. Set
`RP6502_HOME` in the `Makefile` if you want `make TARGETS=rp6502 test` to find it, or call it
directly. A window:

```bash
../rp6502/build/emulator/release/rp6502-emu cc65-Chess-rp6502.rp6502 --scale 2
```

One screenshot, no window, no sound, and a scratch drive so the ROM cannot touch anything:

```bash
../rp6502/build/emulator/release/rp6502-emu cc65-Chess-rp6502.rp6502 \
    --screenshot scratch/title.png --frames 120 --tmpdrive --mute
```

`--phi2 <khz>` sets the 6502 clock between 100 and 8000; the default 8000 is what a
Picocomputer does at its fastest, and dropping it to 1000 is how you see what the game feels
like at C64 speed. `--seed <n>` fixes the RNG for a repeatable run.

## Driving it

`--script <file>` takes one command a line and is the whole instrument:

| | |
|---|---|
| `run [frames]` | let frames elapse |
| `press` / `release <key>...` | the HID bitmap directly, by name or `0xNN` |
| `type "text"` | type into the console |
| `key <name>[+ctrl]` | send a key's escape sequence |
| `wait "text" [frames]` | run until the console says it |
| `expect "text"` / `expect-not` | check the console since the last check |
| `peek [xram:]<addr> <byte>...` | compare memory |
| `dump [xram:]<addr> [count]` | print memory as hex |
| `crc` / `expect-crc <hash>` | the canvas as a CRC-32 |
| `mark`, `expect-same`, `expect-changed` | the canvas against a remembered one |
| `shot "file.png"` | write the canvas |
| `expect-exit <code> [frames]` | run until it exits and check the code |

**Use `press`/`release`, not `type`.** `--input` and `type` feed `RIA.rx`, the console byte
stream; they leave the HID key bitmap clear, and `platRP6502.c` reads the bitmap. Nothing past
the title screen responds to typing.

A press needs a release, and both need frames around them — the game samples the bitmap when
it polls, and a press with no `run` after it can be gone before anything looks:

```
run 90                      # title screen
press enter
run 5
release enter
run 45                      # main menu

press down                  # "2 Human players"
run 5
release down
run 15
press enter
run 5
release enter
run 150                     # the board

shot "scratch/board.png"
```

Menu depth is easy to get wrong and the failure is silent — the presses land in a menu you did
not know was there. From the title: **main menu → colour menu → skill menu → board** for one
human player, and **main menu → skill menu → board** for the two AI-involving entries; "2
Human players" skips the skill menu entirely, which is why it is the cheapest way into a board.

## Reading a screenshot

The canvas is 320x240 and a PNG of it is small on a modern screen; a white outlined piece on a
black square reads as an empty square until you zoom.

```bash
python3 -c "
from PIL import Image
im = Image.open('scratch/board.png')
im.crop((160, 0, 224, 40)).resize((512, 320), Image.NEAREST).save('scratch/zoom.png')"
```

For a regression check prefer `expect-crc` or `dump` over looking. Cells are three bytes —
glyph, foreground, background — 40 to a row, so the board plane cell at column x row y is at
XRAM `((y * 40 + x) * 3)`, and the text plane is the same offset from `$1000`. Square e4's
top-left cell, for instance:

```
dump xram:183 12
```

## Real hardware

```bash
python3 rp6502/rp6502.py -c .rp6502 run cc65-Chess-rp6502.rp6502
```

`-c` names a config holding the serial device or the telnet host and passkey; it is written on
first use. `rp6502.py term` attaches to the console on its own, and `Ctrl-A X` gets out.
