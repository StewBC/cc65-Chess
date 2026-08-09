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

**`cx16` builds here now but is still not *run* here.** The build failure that predated the
engine work was cc65 renaming the software stack pointer: `platCX16.c` used `(sp)` in inline
assembly where current cc65 wants `(c_sp)`. It compiles and links clean, repetition detection
included. Running it still needs the Windows machine, so the rest of the rule stands — do not
delete it, do not knowingly break it, do not edit its platform files speculatively.

**Two targets have video memory the linker cannot see, and both now have a config that
says so.** The Apple II's HGR page 1 at `$2000` and the Atari's GR.8 framebuffer at a
hard-coded `$9100` in `hiresAtari.s`. A clean `ld65` run proves nothing about either: the
Atari linked "inside its budget" for months while drawing BSS onto the top of the screen.
Both cfgs now cap the program below the framebuffer, so an overrun is a link error.

**The Apple II is the tightest machine in the tree**, at 1.2 KB spare in MAIN against the
Atari's 2.7 KB at optsize. The program starts at `$4000` because
HGR page 1 is at `$2000-$3FFF`, and `src/apple2/chessA2.cfg` puts BSS in the stranded
`$0800-$1FFF` below it — verified running, including a write watchpoint over the unused tail.
Before that config existed there were 460 bytes of headroom, and repetition detection would not
have linked. Size anything large against the Apple II first.

**`apple2`, `plus4` and `atari` are no longer in that category.** All three build here.
`apple2` and `plus4` also *run* here — `../a2m-v2` for the Apple II, VICE's `xplus4` binary
monitor for the Plus/4 — so a change to either can be verified instead of argued (§7 of
`doc/measuring.md`). `atari` builds to a `.atr` with `dir2atr`, but running it is still
Altirra on the Windows machine.

**Compiling a target says nothing about whether it runs.** The plus4 build linked inside its
budget throughout the rework and was broken end to end — by a **cc65 bug**, not ours: in
bitmap mode `cgetc()` skips its own wait loop (the branch means to skip the cursor drawing)
and reads a character that is not there, underflowing the Kernal's key count at `$EF`. After
that every key read returns garbage and the menus drive themselves. `plat_ReadKeys` works
around it by checking `$EF` before calling `cgetc()`. Prefer running a target over reasoning
about it.

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

**Nor does a cost measured on this host.** Maintaining the position hash costs 5.5% here and
9% on a real C64 — out by nearly half, because a 16-bit XOR and a table index are one
instruction here and several on a 6502. Price anything that has to hold at the board with
`tests/c64search.c` under VICE; it is headless, runs under warp, and takes a minute. And check
that both builds report the *same node count to the digit* before believing the clock — twice
the harness itself made them play different games.

**Do not measure from the opening position.** It is the optimistic case, not the
conservative one — after eight moves nothing has been traded and the lines are open, so the
middlegame is about 25% slower per node. Anything that has to hold at the board gets
measured over a real game.

## Building and testing

```bash
make OPTIONS=optspeed TARGETS=c64
make TARGETS=apple2 && make po      # bootable chess.po, needs cadius
make TARGETS=atari  && make atr     # bootable cc65-Chess.atr, needs dir2atr
```

The image step is a **second `make`**. `TARGETS=` sets the suffix `$(PROGRAM)` already
carries, so `make TARGETS=apple2 po` asks for `cc65-Chess.apple2.apple2` and fails, and a
bare `make po` has no rule to build the binary it needs.

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

**A feature being measured has to be switchable.** The two sides of a match live in one
binary, so a change gets a flag under `EVAL_TUNING` — `geEvalTerms` for evaluation terms,
`geSearchRepetition` for repetition — present in the tuning build and compiled out of the
8-bit one, which pays neither a byte nor a test.

**The search is deterministic**, so a match without a varied opening set is one game
repeated N times. If you make it non-deterministic, that has to be switchable — every
measurement in `doc/strength.md` depends on it.

## Provenance

This engine was rewritten with AI assistance, and the repo is open about it. `doc/`
documents what was built and how it was measured; `doc/rework-log.md` is the working
journal, kept unsanitised because the wrong turns are the instructive part.
