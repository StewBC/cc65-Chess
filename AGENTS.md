# Working on cc65 Chess

A chess program for 1 MHz 8-bit machines — c64, apple2, atmos, plus4, atari, cx16 — plus a
terminal build used for development. C, compiled with cc65.

`doc/engine.md` explains how the engine works and is the right thing to read first. This
file is the short list of constraints that are easy to violate by accident.

## Hard constraints

**`plat.h` and the 0–63 tile numbering are frozen.** The 0x88 board representation stays
inside the engine and converts at the boundary. This is what lets the platform files that
cannot be built here stay untouched.

**The frozen interface is wider than `plat.h`.** Every port also reads four globals
directly, and this was discovered the hard way:

- `gChessBoard[y][x]` — pieces, for drawing
- the attacker **count** for a tile and side — no port ever reads the attacker list
- `gTile[0]`, `gTile[1]`, `gPiece[1]`, `gColor[0]` — the move log line

**`atari` and `cx16` are not built or tested here** — the tools live on a Windows machine.
Do not delete them, do not knowingly break them, do not edit their platform files
speculatively. Both had pre-existing build failures unrelated to the engine work.

**Keys must not change.** Cursor keys, RETURN, RUN/STOP, `M`, `B`, `A`, `D`, `U`, `R` keep
their current meanings. New functionality that needs a key is a red flag — raise it rather
than inventing a binding. The four-item skill menu is the only difficulty control.

**The attacker/defender visualizer is a feature, not an accident.** The `B` / `A` / `D`
displays are the thing worth preserving most. They may be reimplemented, never degraded.

**All targets must build and play.** Intermediate states may break individual targets; the
tree as committed may not.

## Code style

Tabs for indentation. Match the surrounding comment voice — explanatory, lower-case, no
ceremony.

C89 declarations at the top of a block. `char` for nearly everything: cc65's `char` is
**unsigned** and its `int` is **16 bits**. No new stdlib dependencies in code that reaches
the 8-bit builds; test-only helpers must stay out of the cc65 build entirely.

## Traps that have already caught someone

**The native suite validates logic, never machine width.** Anything that multiplies,
accumulates, or sums toward a limit needs reading against a 16-bit `int` by hand. A node
guard written as `(n + n + n) > budget` wrapped above 21845 and silently never fired at
level 4 — the host build has 32-bit ints, so no native test could ever reproduce it.

**cc65 allows a function only 256 bytes of locals.** A `t_engMove[128]` on the stack builds
on the host and not on the target. Move lists come from the shared arena, never the stack.

**A generator that fills a shared arena must take its capacity as an argument.** The first
version wrote its moves and then checked whether they fitted, overrunning into the statics
that followed. The symptom was a node budget being silently ignored, which looks nothing
like a buffer overrun.

**Running out of arena is a strength problem, not a crash.** Quiescence bailing out to a
static evaluation in exactly the sharp positions it exists for cost real playing strength
and produced no error of any kind.

**A cost measured on perft does not transfer to the search.** Perft makes and tests every
generated move; a search with working move ordering tries about 2.28 moves per node before
a cutoff. Legality dominates the first and not the second. An optimisation justified by
perft numbers has not been justified.

**Do not measure from the opening position.** It is the optimistic case, not the
conservative one — after eight moves nothing has been traded and the lines are open, so the
middlegame is about 25% slower per node. Anything that has to hold at the board gets
measured over a real game.

## Building and testing

```bash
make OPTIONS=optspeed TARGETS=c64
```

```bash
cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c src/search.c \
   src/board.c src/undo.c src/cpu.c src/human.c src/frontend.c src/main.c \
   src/term/platTerm.c -o /tmp/chessterm
```

```bash
cd tests && make test
```

**Never carry a red suite forward.** `doc/measuring.md` covers the rest of the
instrumentation: perft, the fuzzer, match play, the on-target C64 benchmarks, and how to
reproduce the strength figures.

## Measuring a change in strength

**Sixteen games tells you nothing, and it lies confidently.** A king-safety term looked like
a mild win over 16 games and was a 2.6-sigma loss over 512. A 512-game match takes about 35
seconds; there is no excuse for a short one.

**Compare at equal time, not equal nodes.** Every richer evaluation wins at equal nodes.
That is not the question a player is asking, and it overturned two terms that had already
been accepted.

**The search is deterministic**, so a match without a varied opening set is one game
repeated N times. If you make it non-deterministic, that has to be switchable — every
measurement in `doc/strength.md` depends on it.

## Provenance

This engine was rewritten with AI assistance, and the repo is open about it. `doc/`
documents what was built and how it was measured; `doc/rework-log.md` is the working
journal, kept unsanitised because the wrong turns are the instructive part.
