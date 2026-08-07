# cc65 Chess

A chess program for 1 MHz 8-bit machines — Commodore 64, Apple II, Oric, Plus/4, Atari,
Commander X16 — written in C and built with [cc65](https://cc65.github.io/). There is also a
terminal build for development.

It is also, now, a guided tour of how a chess engine works. The engine was rewritten from
scratch, the old one is still in the history, and both are documented in full — including a
measurement of exactly how strong the result is and exactly why the original was weak. If
you have ever wanted to read a complete, small, real chess engine rather than a description
of one, that is what `doc/` is for.

**Written with AI assistance.** The original 2014 program was written by hand; the engine
that replaced it in 2026, and the documentation and test harness around it, were produced
in collaboration with an AI. This is stated up front rather than left to be inferred, and
`doc/rework-log.md` is the unedited working journal if you want to see how that went —
including the parts that went wrong.

---

## How strong is it?

Measured over roughly 20,000 games against Stockfish, using two independent match runners:

| Menu level | Search budget | Time per move on a stock C64 | Approximate rating |
|---|---|---|---|
| 1 — Very Easy | 400 nodes | 8 seconds | ~1170 |
| 2 — Easy | 1,200 nodes | 29 seconds | ~1350 |
| 3 — Harder | 15,000 nodes | ~3.5 minutes | ~1610 |
| 4 — Very Hard | 60,000 nodes | ~15 minutes | ~1700 |

Ratings are on Stockfish's own scale, ±150 — and that uncertainty is honest rather than
statistical. At its strongest setting the engine draws level with Stockfish restricted to a
single search node. `doc/strength.md` explains what the numbers mean, what they do not mean,
and how to reproduce them.

Emulator speed-up is a free multiplier: strength is measured in positions searched, not
seconds, so an accelerated machine plays the same game sooner. Roughly 60 rating points per
doubling of thinking time.

## Playing it

Built images for every platform are in the releases tab. There is a video of the game
[here](http://youtu.be/bkA4vtwxaJg).

**Keys.** Cursor keys move the cursor; `RETURN` selects a piece and then its destination, or
deselects it. `M` (or `RUN/STOP` with nothing selected) opens the menu. `U` and `R` undo and
redo — the stack holds the last 127 moves, and in a game against the AI an undo takes back
both plies so you can play something else.

**The cursor colour tells you what you are on:** green — selectable, red — a piece with no
legal moves, purple — an empty square or an enemy piece, blue — the piece you have selected,
cyan — a square it can move to.

**Three keys show you the attacks**, and they are the feature the whole program is built
around. `B` overlays every square with how many white and black pieces attack it. `A`
highlights every enemy piece attacking the square under the cursor. `D` highlights every
friendly piece defending it. Attackers in cyan, defenders in red.

## Building

```bash
make OPTIONS=optspeed TARGETS=c64
```

Build for speed on almost every target — it is also *smaller* on most of them. The Atari is
the exception and needs `OPTIONS=optsize` for the extra kilobyte.

`make` with no `TARGETS` builds the c64. The full list is `apple2 atari atmos c64 c64.chr
plus4 cx16`. Most platforms have a second step to produce a disk, tape or program image —
`dsk`, `atr`, `tap`, `prg`, `cprg`, `cxprg` — and the two can be combined:

```bash
make OPTIONS=optspeed all dsk atr tap prg cprg cxprg
```

**The terminal build**, which is what development happens against:

```bash
cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c src/search.c \
   src/board.c src/undo.c src/cpu.c src/human.c src/frontend.c src/main.c \
   src/term/platTerm.c -o /tmp/chessterm
```

**Current state:** `c64`, `c64.chr`, `apple2`, `atmos` and `plus4` build and link inside
their budgets. `atari` and `cx16` have build failures that predate the engine rework and look
like cc65 version drift.

## The documentation

| | |
|---|---|
| [doc/engine.md](doc/engine.md) | **How the engine works.** The main event. Board representation, move generation, evaluation, alpha-beta, quiescence — every idea tied to the function that implements it. Assumes you know how the pieces move and nothing else. |
| [doc/original-engine.md](doc/original-engine.md) | **How the engine that was replaced worked, and why it was weak.** Two structural decisions, both of them traps that are well known inside computer chess and nearly invisible from outside it. |
| [doc/strength.md](doc/strength.md) | **How strong it is and how that was established.** The measurement, the methodology, and an honest account of what the numbers are worth. |
| [doc/measuring.md](doc/measuring.md) | **The instruments.** What to run, which question each answers, and the workflow for changing the engine without breaking it. |
| [doc/rework-log.md](doc/rework-log.md) | **The working journal**, published unsanitised. Its value is the measurements that demolished the plan around them. |
| [doc/readme-2014.txt](doc/readme-2014.txt) | The original readme, kept verbatim as a historical document. Its description of the AI describes the *old* engine, which is the point. |

Contributors and agents should also read [AGENTS.md](AGENTS.md) — the constraints that are
easy to violate by accident, and the traps that have already caught someone.

## Tests

```bash
cd tests && make test
```

21 seconds, exits non-zero on failure. Move generation is verified against perft — the
standard correctness test — exactly to depth 5 on all five reference positions, and to depth
6 on the two that publish one. Beyond that there is a game fuzzer, a match harness for
measuring whether a change actually made the engine stronger, on-target benchmarks that run
under VICE, and a UCI adapter so the engine can be played against other engines or opened in
any chess GUI. [doc/measuring.md](doc/measuring.md) covers all of it.

## Porting

Everything platform-specific is behind `plat.h`, and the board is presented to it as squares
0–63 regardless of what the engine does internally. When the terminal port was written it
took about an hour. The engine rewrite touched no platform file at all, including the ones
that cannot be built here.

If you port it somewhere new, please say so — that is the best part of putting this online.

## History and credits

Started in February 2014, three months after the author learned to play chess, as a "for the
fun of it" project for the Commodore 64. The engine was replaced in 2026; everything else —
the display, the menus, the undo stack, the attack visualizer, the ports — is the original
program.

- **Stefan Wessels** — the program, and all of it before 2026.
- **Oliver Schmidt** — the Apple II port, and the generic cc65 Makefile with Patryk
  "Silver Dream !" Łogiewa.
- **[raxiss]** — the Oric-1 / Atmos / Telestrat port.
- **Ullrich von Bassewitz and the cc65 team** — the compiler that makes all of this possible.

The tag `v1-original-engine` marks the last commit before the engine rewrite, if you want to
check out the original and compare.

## Licence

Public domain — see [LICENSE](LICENSE). Do anything you like with it.
