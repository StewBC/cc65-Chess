# Native MEGA65 (LLVM-MOS and Xemu)

This port starts in native BASIC 65, uses 45GS02 instructions and enables VIC-IV
fast mode. It never enters GO64. The shipping engine, four node budgets, keys and
complete B/A/D visualizer are unchanged. The 320x200 bitmap uses the existing
32x24 Commodore artwork and the C64 Pepto PAL palette shipped with VICE.
Inactive menu entries use light gray rather than dark gray.

## Build and play

```sh
export LLVM_MOS_SDK=/Volumes/EXTERNAL/ext-dev/llvm-mos
make mega65
make mega65 test
```

Product: `build/mega65/chess.prg`. Compiler: `mos-mega65-clang`, `-Oz`, unsigned
char, normal shipping engine. LLVM-MOS has 16-bit integers on this target.
The new toolchain is opt-in; it does not join the default build.

The run recipe defaults to this machine's Xemu installation and ROM:

```sh
export XEMU_MEGA65=/Volumes/EXTERNAL/ext-dev/xemu/build/bin/xmega65.native
export MEGA65_ROM=/Volumes/EXTERNAL/ext-dev/xemu/rom/MEGA65.ROM
make mega65 test
```

Set those variables to use other paths. The recipe supplies an absolute ROM
path, and `-prgmode 65` explicitly. The ROM is not distributed with this project.
On hardware, load the PRG from native BASIC 65 and RUN it.

## Memory

`src/mega65/link.ld` overrides the SDK script by linking from its directory.
Merely adding another `-T` duplicates the SDK's memory region, and putting a
local directory at the end of `-L` still selects the SDK's script first.

| Area | Address |
|---|---|
| PRG, data and BSS | $2001-$97FF |
| reserved 2K software stack | $9800-$9FFF, grows down from $A000 |
| bitmap | $A000-$BFFF |
| screen colour matrix | $C000-$C3FF |

The program region is capped below the stack and bitmap, so growth fails at
link time. VIC-IV precise pointers select the display RAM. Display registers
and the palette are restored on exit. The SDK handles BASIC mapping on return.
`save-basic.o` is explicitly linked:
without it the SDK restores mapping but its default exit spins forever. The
return-to-BASIC check caught this; Quit now reaches an interactive READY prompt.

## Search benchmark

```sh
mkdir -p scratch
make mega65 mega65-bench
"$XEMU_MEGA65" -rom "$MEGA65_ROM" \
  -prg "$PWD/build/mega65/bench.prg" -prgmode 65 \
  -uartmon /tmp/chess-mega65.sock -sleepless
```

Leave Xemu running. In a second terminal, from the repository root:

```sh
python3 tests/mega65bench-run.py > scratch/mega65-bench.json
```

The benchmark deliberately spins after setting its completion byte so its
records remain readable. Close this Xemu instance after collecting them.
`-sleepless` accelerates emulation without changing the CIA timer measurement.
The reader gets symbol addresses from the freshly generated `bench.map`;
never reuse addresses from an earlier build.

The harness searches six developed positions (plies 16-21 of the retained
C64 profiler's fixed game), at each of the shipping skill budgets. It replays
the fixed moves rather than the search's chosen moves so every level sees the
same positions. Node counts and a checksum cover nodes, score, move, flags and
depth. A timing comparison is valid only when both match exactly.

CIA2 A and B form a cascaded 32-bit timer. The recorded milliseconds use
a nominal 1 MHz timer clock; an NTSC C64 runs its CIA at about 1.023 MHz,
so convert by that factor for precise seconds. Each search's elapsed ticks are
converted to milliseconds before adding to the six-position total; summing
microseconds directly would overflow on the C64 at level 4. This timer setup
belongs only to the standalone benchmark. It is absent from the game.

A host reference can be built with:

```sh
cc -O2 -funsigned-char -Isrc src/engine.c src/eval.c src/search.c \
  tests/mega65bench.c -o scratch/mega65bench-host
scratch/mega65bench-host
```

The identical C64 harness builds without leaving objects beside sources:

```sh
mkdir -p build/obj/mega65bench build/mega65
for unit in engine eval search; do
  cl65 -t c64 -Or -Isrc -c -o "build/obj/mega65bench/$unit.o" "src/$unit.c"
done
cl65 -t c64 -Or -Isrc -c -o build/obj/mega65bench/bench.o tests/mega65bench.c
cl65 -t c64 -Ln build/mega65/bench-c64.lbl -o build/mega65/bench-c64.prg \
  build/obj/mega65bench/engine.o build/obj/mega65bench/eval.o \
  build/obj/mega65bench/search.o build/obj/mega65bench/bench.o
```

The measured C64 comparison used VICE 3.10, NTSC, `-warp`, `-console`, and a
binary monitor. [The measurement guide](../doc/measuring.md#6-on-target-measurement)
explains VICE's macOS environment and ROM lookup. The monitor client is
`recipes/vice/vicebin.py`. Resolve addresses from `bench-c64.lbl`, read memory,
and send `CMD_EXIT` to resume execution between polls. Reading the monitor
without resuming would time a paused program.

Read `_bench` (four ten-byte records: 32-bit nodes,
32-bit milliseconds, 16-bit checksum) after `_benchDone` becomes 1. A value
of 255 means the fixed game could not be replayed.

Emulated time is evidence about emulator behaviour. Confirm speed on the real
MEGA65 before treating it as a hardware measurement. Xemu's default fast clock
in the tested build is 40.5 MHz; do not use host process elapsed time as the
MEGA65 clock.

## Verification, 2026-10-12

LLVM-MOS 24.0.0git, revision `9e5efd81a0b7c23e62e8ef90d298f2ddbbb4ed35`;
Xemu master `40dfef0d1d5f56be2469492715c12bdb32c75b67`.
Native suite green; existing C64, C64 character, Apple II and Atari targets
build. Headless Xemu verified startup, two-player and one-player menus,
e2-e4 and an AI reply, undo/redo board restoration and log, and B/A/D.
Quit returns to BASIC with its READY prompt.
Keyboard input was injected into this ROM's GETIN buffer ($02B0, count $D0),
not by substituting a test input handler in the game. Framebuffer screenshots
were inspected. Generated logs, images and emulator copies stay in scratch/.

All four native search result checksums match the host:

| Level | Nodes across six positions | Checksum |
|---|---:|---|
| 1 | 2072 | b72f |
| 2 | 7200 | c703 |
| 3 | 70225 | 8671 |
| 4 | 362542 | 8936 |

The final Xemu run totals 707 / 2648 / 20102 / 124357 nominal timer
milliseconds across the six positions. VICE completed all six positions at
levels 1/2 in 87474 / 328221 nominal milliseconds. After correcting the NTSC
CIA clock, the average search times are:

| Skill | Native MEGA65, Xemu | C64, VICE | Speed-up |
|---|---:|---:|---:|
| Very Easy | 0.118 s | 14.255 s | 121x |
| Easy | 0.441 s | 53.488 s | 121x |
| Harder | 3.350 s | not completed | — |
| Very Hard | 20.726 s | not completed | — |

These are searches in six fixed middlegames, not a claim about every move.
Opening-table moves can return without a search. Levels 1/2 have identical
nodes and checksums on both targets. Native levels 3/4 matched the host; the
longer C64 baseline was stopped and its partial rows discarded.

The comparison combines the faster emulated CPU, 45GS02 execution and
LLVM-MOS code generation. It does not isolate those effects: the clock ratio
is about 40x and the combined speed-up about 121x, but attributing the remaining
factor to the compiler alone has not been measured. Physical MEGA65 timing
and an A3010 comparison remain unmeasured. The A3010's lower clock rate alone
cannot predict its relative chess performance.

Palette follow-up: the owner requested C64 colours. The port now programs
VICE's Pepto PAL RGB values, including its standard green, and uses light gray
for inactive menu text. The menu screenshot was rechecked after rebuilding.
