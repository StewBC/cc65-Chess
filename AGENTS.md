# Working on cc65 Chess

A chess program for 1 MHz 8-bit machines — c64, apple2, atmos, plus4, atari, cx16, rp6502 —
plus a terminal build used for development. C, compiled with cc65.

`doc/engine.md` explains how the engine works and is the right thing to read first. This
file is the short list of constraints that are easy to violate by accident.

`doc/rework-log.md` is the working journal, and its last section is the closed search and
evaluation portfolios. Read that before proposing a search technique or an evaluation term.
B3+B4 shipped (−17.2% per node on a C64). The skill budgets are 400 / 1,200 / 18,000 /
65,000. Do not reopen a rejected candidate by renaming it.

## Hard constraints

**`plat.h` and the 0–63 tile numbering are frozen.** The 0x88 board representation stays
inside the engine and converts at the boundary. This is what lets the platform files that
cannot be built here stay untouched.

**There is one addition to `plat.h` since the freeze, and the bar it had to clear is the
point.** `plat_GetSeed()` returns a byte of entropy for the opening randomiser. It went in
because every machine has a free-running counter, cc65's own `asminc` names all of them, and
the implementation is three lines a port that reads a register and cannot fail visibly — a
wrong address gives repeating openings, which is what the engine did anyway. Anything asking
to be added here should be held to that: no writes to hardware on a port that cannot be run,
and a failure mode nobody has to debug. Note there are **nine** platform files, not seven —
`c64.chr` is a separate port and `term` is the one that must return a constant.

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

**`rp6502` is the one target with no framebuffer to collide with at all** — its video memory
is XRAM on the VGA co-processor, off the 6502 bus, reached through `RIA.addr0`/`RIA.rw0`. But
a clean `ld65` still does not prove everything there either: stock `rp6502.cfg` runs its RAM
area to `$F6FF` and `crt0.s` then starts the 2K C stack at `$F700` growing *down* into it, so
the real ceiling is `$EF00` and the last 2K of the linker's own area is not free. With 26K
spare that is a note, not a risk.

**Every target is built at the same optimisation setting, and that setting is `optsize`.**
Uniformity is the point - a port that is built differently is a port that behaves
differently - so `optspeed` is not an option for one target while another cannot take it.
Since the endgame tables it is also the only setting that fits:

| | optsize | optspeed |
|---|---|---|
| atari | 874 free below the framebuffer | **does not link — 1942 bytes over** |
| apple2 | 656 free in MAIN, 2024 in BSS | **does not link — 1035 bytes over** |

**The Atari's headroom does not shrink smoothly, and the number above hides a cliff.** `DLIST`
is page-aligned inside `MAIN`, so code growth is absorbed by the padding in front of it until
the padding runs out and the display list jumps a page — 256 bytes, gone at once. That has now
happened three times: the opening table crossed it, check evasions crossed it again, and the
black reply table crossed it a third time, which is why the Atari lost 256 bytes each time
while the Apple II lost the 302, 132 and 253 the code actually costs.

**There are 38 bytes of padding left.** Check `DATA`'s end against `DLIST`'s start in an
`ld65 -m` map rather than reading the free-space number on its own; the two tell different
stories and only one of them predicts what the next change will cost. Free space is
*ceiling − first unused* (`__MAIN_LAST__` on the Apple II, map End+1 elsewhere). The
full picture, all eight targets, measured rather than remembered — free space to the
*real* ceiling, which on three of them is a framebuffer the linker cannot see:

| target | ceiling | free |
|---|---|---|
| **apple2** | MAIN ends `$B700` | **656**, plus 2024 in BSS at `$0800–$1FFF` |
| **atari** | `$9100` framebuffer | **874** — and 38 before the next `DLIST` page jump (`DATA` `$7ED9`, `DLIST` `$7F00`) |
| atmos | RAMEND `$9900` less stack | 1875 |
| plus4 | `$A000` bitmap | 2240 — **the cfg does not cap it**, so an overrun would draw BSS on the screen rather than fail to link, exactly as the Atari used to |
| cx16 | HIMEM `$9F00` less stack | 3024 — the framebuffer is in VERA and costs nothing here |
| c64.chr | BSS ends `$C400` | 8178 |
| c64 | `$C000` bitmap less stack | 10366 |
| rp6502 | `$F700` c_sp less the 2K stack | **26430** — video memory is XRAM and costs the 6502 nothing |

Regenerate these with `cl65 -t <target> -C <cfg> --mapfile ...` against `build/obj/<target>/*.o`;
the numbers above are from a clean build and will drift with the next change.

`Makefile.options` defaults to `optsize` and that is why. Raising it would mean raising it
everywhere, which the Atari cannot take - so treat `optsize` as fixed, and check anything
added to the shipped build against the two numbers above.

The 1,024-byte persistent undo ring now lives at `$AF00–$B2FF` on Atari only, in the hole
between the GR.8 screen and the 2K software stack. That is why the framebuffer headroom
above is a kilobyte larger than BSS growth would suggest. 288 bytes remain as a pad at
`$B300–$B41F`. The start address has only been tested down to `$1000` (`$0800` crashes on
load — MyPicoDOS is still there while it works).

Both tight targets got there the same way. The Apple II starts at `$4000` because HGR page 1
is at `$2000-$3FFF`, and `src/apple2/chessA2.cfg` puts BSS in the stranded `$0800-$1FFF`
below it — verified running, with a write watchpoint over the unused tail. Before that config
existed it had 460 bytes and repetition detection would not have linked.

**`apple2`, `plus4` and `atari` all build here, and two of the three run here.**
`apple2` and `plus4` also *run* here — `../a2m-v2` for the Apple II, VICE's `xplus4` binary
monitor for the Plus/4 — so a change to either can be verified instead of argued (§7 of
`doc/measuring.md`). `atari` runs here too now, under AltirraSDL's JSON bridge — see
`recipes/altirra-bridge-usage.md`, which is how the framebuffer collision was found and the
fix confirmed. Emulator how-tos for the targets that run here live in `recipes/`.

**`rp6502` runs here better than any of them, and there is no excuse for guessing about it.**
`rp6502-emu --script` drives the HID keyboard, waits on console output, dumps RAM and XRAM,
checks a canvas CRC and writes a PNG, all headless and all exiting non-zero on a failed check.
Every screenshot in `recipes/rp6502-emulator.md` was produced that way. Note the one thing the
script cannot be replaced by: `--input` feeds `RIA.rx` and leaves the HID bitmap clear, and
this port reads the bitmap — so `press`/`release` are what move the cursor, not `type`.

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

**`B` shows four numbers a square, and all four are deliberate**: attackers bottom left,
defenders bottom right, piece value top left, colour top right. The C64 separates them by
colour and says so in a comment; the Apple II has no colour to spare and crams the top pair
into three hex characters, which reads like a debug leftover and is not one. It is the display
the 2014 video explains. Before deleting anything in a port that looks like debris, read the
same function in `platC64.c` - the ports are ports.

**All targets must build and play.** Intermediate states may break individual targets; the
tree as committed may not.

## Code style

Tabs for indentation. Match the surrounding comment voice — explanatory, lower-case, no
ceremony.

C89 declarations at the top of a block. `char` for nearly everything: cc65's `char` is
**unsigned** and its `int` is **16 bits**. No new stdlib dependencies in code that reaches
the 8-bit builds; test-only helpers must stay out of the cc65 build entirely.

## Traps that have already caught someone

**A test that runs at one budget has not tested the skill levels.** The tactics suite searched
every position with 60,000 nodes — the level 4 budget — so for the life of the project nothing
had asked levels 1 and 2 whether they could see a mate in one. They could not: level 1 solved
27 of 60, and a person playing on an Apple II found it before any test did. Anything that
depends on how much the engine searches has to be exercised at `gcSearchSkill`'s own numbers.

**Self-play cannot see a defect both sides share.** That is twice now: the repetition
blindness and the mate blindness both produced perfectly balanced matches. A balanced
result means the two configurations are equal, not that either is right.

**A green suite can be a stale binary.** `tests/Makefile` did not list the engine headers as
prerequisites, so editing `search.h` and running `make test` reported green for code that was
not in the binary. It does now — but note that `sed -i.bak` followed by `mv` restores the
*original mtime*, which walks straight back into it. `touch` the header, or `make -B`.

**And a *sub-second* mtime is enough to do it.** Patching a source, building, and running
inside one second lets `make` decide the binary is newer than the file it was not built from.
This bit while checking the black reply table: three deliberately broken tables in a row all
reported green, which read as a useless test and was a stale one. Use `make -B` for anything
that patches a source and rebuilds in a loop. Related, on macOS: `sed` does **not** expand
`\t` in a pattern, so a tab-indented table silently never matches and the "control" edits
nothing at all.

**A per-game count is not a per-move cost, and it will correlate with the outcome for free.**
Counting cc65's early queen moves per game against Sargon gave losses 4, 4, 4 and wins 0, 1 —
which is what a real effect looks like and also what "a losing position invites queen moves"
looks like. Cost per move reversed it (rooks twice as expensive), and cost per move *split by
game phase* reversed it back, because rooks barely move before move 15. Three statistics over
one set of six games and three different answers — and then a **fourth** found the third was
fragile too, because all three were plain means over evaluation deltas with mate scores in
them. Controlling for a confounder does not make a statistic robust; those are two different
problems and this table had both.

**A mean over Stockfish evaluation deltas is not a robust statistic, and the instrument said so
about a different column.** Mate scores clamp at ±10000, so one clamped move is worth two
hundred ordinary ones. Over 64 games, 26 clamped moves in 547 turned the mean cost of a middlegame king move from
−23.5 into **−289.3**, twelve times its own robust value, and 15 in 661 turned a pawn move from
−20.6 into **+68.5** — a pawn move that reads as a gain. `analyse.py` has always
warned that the clamp confuses blunder counts; nobody asked what it did to the averages, and
the table that started this was fourteen queen moves over six games computed exactly that
way. Drop the mate scores and print a median beside the mean. `doc/rework-log.md` has the
replication.

**A mechanism that explains a measurement is not the cause of it, and turning the mechanism
*up* is the cheap way to find out.** The queen was measured costing four times a pawn per move
in the opening, and `sc_pstQueen` paying it ten centipawns to leave home was a mechanism that
fitted perfectly. Removing those ten centipawns measured −1.1 sigma over 6,144 games; tripling
them, which had to measure worse if the square mattered at all, measured **+0.4 sigma**. Both
directions ran backwards, so the square is inert and the explanation was wrong while the
measurement it explained was right. The dose cost twenty minutes and settled what another
candidate table would not have. `doc/rework-log.md` has the dose.

**A test that only asks for a legal move cannot see a dead table entry.** The first version of
the black reply check asked that the reply be legal and that more than one distinct reply come
up. A table entry written with the wrong squares does not play an illegal move — it matches
nothing, falls through to the search, and the search's reply is a legal move the other seeds
did not play. A broken half-entry therefore looks *exactly* like working variety. The check
has to name the moves it expects.

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

Emulator how-tos for the targets that run here — Apple II, Plus/4, Atari, C64, Picocomputer —
are in `recipes/`. Generated match output stays in `scratch/`.

## Building and testing

```bash
make c64
make apple2 po              # bootable build/apple2/cc65-Chess.po, needs cadius
make atari atr              # bootable build/atari/cc65-Chess.atr, needs dir2atr
make rp6502 rom             # build/rp6502/cc65-Chess.rp6502, needs python3
make spectrum               # z88dk → build/spectrum/chess.tap
make term                   # host curses → build/term/chessterm
```

Products land in `build/<port>/`. A port name selects that port, so `make apple2 po` is
one invocation — the old two-step `TARGETS=` / suffix collision is gone. `TARGETS=c64`
still works. `make list` shows which compilers this machine has.

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

**Ask where a term runs, not only what it costs.** `eval_MoveDelta` can only carry a term that
is a property of *a piece on a square*, and for five phases that was treated as the boundary of
what the evaluation could contain. `mateDrive` in `eval.c` is not such a term — it takes both
kings — and it is in the engine anyway because it sits inside the phase test, where it cannot
execute in a middlegame at all and the positions it does run in are ones the search walks at a
few hundred nodes. It is not an oversight and it must not be "corrected" into a running total;
`doc/engine.md` §5.4b is the argument.

**W-L-D cannot see an engine that wins positions and does not finish them.** Two defects here
have had that shape. `chesstest convert` asks for a mate before the fifty-move rule rather than
for a result, and the conversion split in the match harness separates "drew still a piece up"
from "gave it back" — those want different fixes.

**A gate has to be measured, not reasoned about, and self-play cannot check one.** The mate
drive's first version passed every test in `tests/` and scored *better* on the self-play
conversion metric than the version that shipped — and lost twenty points against Sargon II,
because its gate let it change moves in middlegames. The endgame tests contain only bare-king
positions, so they never exercised it out of range, and in self-play both sides carried the
same harm. Any term with a gate needs an outside opponent before it is believed.

**A self-play number is a reason to go and measure, not a result.** It overstated repetition
detection about threefold and understated the endgame tables by half — same instrument, both
directions — so there is no correction factor, only an outside opponent. `doc/rework-log.md`
has both measurements.

**A feature being measured has to be switchable.** The two sides of a match live in one
binary, so a change gets a flag under `EVAL_TUNING` — `geEvalTerms` for evaluation terms,
`geSearchRepetition` for repetition — present in the tuning build and compiled out of the
8-bit one, which pays neither a byte nor a test. `make uci-tuning` puts those switches on the
UCI adapter so a term can be A/B'd against Stockfish rather than against the engine itself;
nothing published is measured with that binary, and it has to reproduce `uci`'s games to the
digit before its differences mean anything.

**The search is deterministic**, so a match without a varied opening set is one game
repeated N times. If you make it non-deterministic, that has to be switchable — every
measurement in `doc/strength.md` depends on it.

**Every defect that mattered here was found by playing, not by measuring, and the reason is
structural: self-play cannot see a weakness both sides share.** Three times now — repetition
detection in Phase 5, mate in one in Phase 10 (found by a player at a board, after 40,000
measured games had not), and the mate drive's gate in Phase 11, where every instrument in
`tests/` preferred the version that lost twenty points to Sargon II. Internal harnesses tell you
whether a change is consistent with the evaluation you already have. They cannot tell you it is
right. Play the thing.

## Provenance

This engine was rewritten with AI assistance, and the repo is open about it. `doc/`
documents what was built and how it was measured; `doc/rework-log.md` is the working
journal, kept unsanitised because the wrong turns are the instructive part.
