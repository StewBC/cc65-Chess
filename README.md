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

Measured over roughly 40,000 games against Stockfish, using two independent match runners:

| Menu level | Search budget | Time per move on a stock C64 | Approximate rating |
|---|---|---|---|
| 1 — Very Easy | 400 nodes | 13 seconds | ~1240 |
| 2 — Easy | 1,200 nodes | 46 seconds | ~1430 |
| 3 — Harder | 15,000 nodes | ~11 minutes | ~1700 |
| 4 — Very Hard | 60,000 nodes | ~45 minutes | ~1950 |

Ratings are on Stockfish's own scale, ±150 — and that uncertainty is honest rather than
statistical. At its strongest setting the engine scores 71% against Stockfish restricted to a
single search node and draws level with it at a hundred. `doc/strength.md` explains what the
numbers mean, what they do not mean, and how to reproduce them.

Emulator speed-up is a free multiplier: strength is measured in positions searched, not
seconds, so an accelerated machine plays the same game sooner. Roughly 60 rating points per
doubling of thinking time.

The table is current as of check evasions in quiescence — the fix for a defect a *player*
found rather than a test: the two weak levels could not see a mate in one, because quiescence
stood pat and looked only at captures even when the king was in check. Level 1 solved 27 of 60
mates in one before and 55 after. `doc/strength.md` §5.1.5 has it, along with the reason 40,000
measured games never noticed — and the on-target measurement showing what it costs, which is
why the times above are longer than they used to be.

The ratings are unchanged by the most recent fix, and the fix is the best story in the project.
Played against **Sargon II** — a 1978 program, on an Apple II — the engine kept drawing games it
had already won: it reached king and rook against a bare king on move 66 of one game, still had
it on move 115, and drew by the fifty-move rule. The cause was that the endgame king table sends
*both* kings to the centre, including the one being mated, so with bare kings every move scored
the same. Basic won endings finished before the fifty-move rule went from 42 to **75** out of
100 at level 1, and against Sargon the White column went from 7 wins and 9 draws to **14 wins
and 2 draws** with no losses either way.

The first version of that fix scored **better** on every test in this repository and was a
twenty-point regression in real games — its gate was loose enough to change moves in
middlegames, which only an outside opponent could see, because self-play applies the same harm
to both sides. `doc/strength.md` §5.1.6 keeps the whole thing, wrong turn included.

That same Sargon match left one number unexplained: 85.9% with White against 25.0% with Black.
This file used to call it an opening-book gap. **It was one lost game, played fourteen times.**
All eighteen Black losses came from two openings; the fourteen `1.e4 Nc6` games were identical
for all 103 plies; outside those two lines Black scored 57% and did not lose once. Play the
engine from positions where no book fires and it scores the same with either colour — at level
1, to the digit. A book helps by spreading the sample, not by improving the moves.

Black now has a reply table too, thirty bytes of it. Played against Sargon II it took the Black
score from 25.0% to **45.3%**, but the number worth reading is the other one: **five distinct
games in thirty-two became twenty-four**, and the most-repeated game went from fourteen copies
to five. The first attempt at the table put a *new* game in the same trap — one entry lost all
21 of its games over 2 distinct games — and neither desktop measurement predicted it; the
losing reply ranked second on score and **first** on a measure built specifically to count
variety. Only the real opponent could see it.

The thing that made the original mistake possible is the better story: **no harness in this
repository could reach the opening table at all.** The UCI adapter called the search directly, and the Sargon harness had a copy of the
table written out again in Python. It shipped for two releases with nothing able to play it.
`doc/strength.md` §5.1.7.

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
make                    # every target
make TARGETS=c64        # or just one
```

The full target list is `apple2 atari atmos c64 c64.chr plus4 cx16`, and a bare `make` builds
all of them. Every one is built at the same optimisation setting, `optsize`, which is the
default — the Atari does not fit at `optspeed`, and a port built differently is a port that
behaves differently.

Most platforms have a second step to produce a disk, tape or program image — `d64`, `dsk`,
`po`, `atr`, `tap`, `prg`, `cprg`, `cxprg`. The binaries have to exist first, so build them in
the same invocation:

```bash
make all d64 dsk po atr tap prg cprg cxprg
```

`po` needs `cadius` and `atr` needs `dir2atr`; the rest need nothing extra.

`cx16` needs a reasonably current cc65: it uses `c_sp` in inline assembly, which older
versions called `sp`.

**The terminal build**, which is what development happens against:

```bash
cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c src/search.c \
   src/board.c src/undo.c src/cpu.c src/human.c src/frontend.c src/main.c \
   src/term/platTerm.c -o /tmp/chessterm
```

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

35 seconds, exits non-zero on failure. Move generation is verified against perft — the
standard correctness test — exactly to depth 5 on all five reference positions, and to depth
6 on the two that publish one. Beyond that there is a game fuzzer, a match harness for
measuring whether a change actually made the engine stronger, on-target benchmarks that run
under VICE, and a UCI adapter so the engine can be played against other engines or opened in
any chess GUI. [doc/measuring.md](doc/measuring.md) covers all of it.

## Porting

Everything platform-specific is behind `plat.h`, and the board is presented to it as squares
0–63 regardless of what the engine does internally. When the terminal port was written it took
about an hour. The engine rewrite touched no platform file at all; the only addition to that
interface since is `plat_GetSeed()`, three lines a port that read a free-running counter for
the opening randomiser.

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
