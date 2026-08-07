# Measuring this engine

`doc/engine.md` explains how the engine works. `doc/strength.md` says how well it plays and
what that claim rests on. This document is the practical one: what the instruments are, how
to run them, and which one answers which question.

There are two reasons to be here.

**You want to check the numbers.** Jump to §5 — the strength figures are reproducible from a
clean clone in a few minutes, and the ladder is deterministic, so you should get the same
table rather than a similar one.

**You want to change the engine without breaking it.** Read §1 and §6. The short version is
that there are four instruments, they answer different questions, and the cheap one does not
substitute for the expensive one.

---

## 1. The four instruments

| Instrument | Answers | Cost |
|---|---|---|
| The pass/fail suite | Is it still correct? | 21 seconds |
| The internal match harness | Is this change stronger? | 35 seconds a comparison |
| The external gauntlet | How strong is it, really? | minutes |
| The C64 benchmarks | What does it cost on the actual hardware? | minutes, plus an emulator |

Plus a fifth that is not a measurement at all — driving the real UI (§7) — which catches the
things the other four cannot, because they all bypass the game and talk to the engine
directly.

**None of these substitutes for another.** A change can be correct, win at equal node counts,
and still lose on real hardware because every node got slower. That is not hypothetical; it
happened twice, and both evaluation terms were removed as a result.

---

## 2. The gate

```bash
cd tests && make test
```

21 seconds, exits non-zero if anything failed. **Never carry a red suite forward.** In order,
it runs:

| Suite | What it establishes |
|---|---|
| `castle` | Castling and en passant rules, case by case |
| `fuzz` ×2 | 300 random games through the *game* path, with undo and redo checked after every move, and the running evaluation checked against a full recount |
| `eperft` to depth 5 | Move generation exact against the five standard reference positions |
| `qgen` | The capture-only generator is an exact subsequence of the full one |
| `tactics` | The search finds the obvious moves |
| `alwaysmoves` | A move is returned whenever one exists, at any budget down to a single node |
| `match sanity` | A configuration playing itself comes out exactly level |
| `selfplay` | A full game at each skill level, with timings |

Two of those are worth understanding rather than just running.

**Perft** — counting leaf nodes to a fixed depth — is the standard correctness test for a
move generator, and it is unforgiving: a single wrongly generated or wrongly rejected move
changes the count. The engine is exact on all five reference positions to depth 5, and to
depth 6 on the two that publish one.

**`alwaysmoves` exists because of a bug that reached a real board.** The search returned "no
move" when its budget ran out before the first iteration finished, and every caller reads
that as checkmate or stalemate — so the AI silently resigned a sound position. Running out of
time must degrade to a *worse move*, never to *no move*. The test asserts that at budgets far
below anything the game would ever use, because the property has to hold at any budget.

### Individual suites

```bash
./chesstest                       # usage
./chesstest eperft 6 -v           # perft deeper, verbose
./chesstest divide <fen> <depth>  # per-move node counts - what to use on a perft mismatch
./chesstest fuzz 5000 500         # different seed, more games
./chesstest budget                # nodes needed to complete each depth, through a real game
./chesstest bench                 # search speed on this host
./chesstest selfplay 4 200 -v     # AI against itself
```

When perft disagrees with the reference, `divide` is the tool: it prints the node count under
each root move, so you compare against any other engine's divide output and descend into
whichever move differs. A perft mismatch is a stop-the-line event — it means make/unmake or
the generator is wrong, and every measurement downstream of it is meaningless.

---

## 3. Measuring a change in strength

```bash
./chesstest match sanity     # must come out exactly level
./chesstest match terms      # which evaluation terms earn their bytes
./chesstest match depth      # does searching deeper actually win
./chesstest match time       # the same comparison at equal TIME
./chesstest match ladder     # does each skill level beat the one below
./chesstest match endgame    # terms that only apply once the queens are off
```

Two configurations play 512 games — 256 generated openings, each twice with the colours
swapped — and the score comes back as wins/losses/draws.

**Three rules, each of which was learned by getting it wrong.**

**Sixteen games tells you nothing, and it lies confidently.** The same comparison at three
sample sizes:

| Comparison | 16 games | 128 games | 512 games |
|---|---|---|---|
| +piece-square vs material only | 9-1-6 | 70-5-53 | overwhelming |
| +king safety vs +piece-square | 4-2-10 *(looks good)* | 28-39-61 | **104-145-263, −2.6σ** |

King safety looked like a mild win and was a real loss. A 512-game match takes 35 seconds.

**Compare at equal time, not equal nodes.** Every richer evaluation wins at equal node counts
— it is strictly more information per node. The question a player is asking is what happens
in the same number of *seconds*, and on a 1 MHz machine that is not a rounding error:

| Term | At equal nodes | Cost per node | At equal time |
|---|---|---|---|
| Pawn structure | +2.0σ | 1.35× slower | **+0.6σ — nothing** |
| Endgame king table | +1.9σ | 1.28× slower | not worth measuring |

Both were removed, and both are good terms. `match time` is the comparison that decides.

**`match sanity` is the check on the instrument itself.** A configuration against itself must
come out exactly level, because the harness plays every opening twice with the colours
swapped, so every result has its mirror. If that is not balanced, the harness is measuring
something other than the change and no other result from it means anything.

---

## 4. The external gauntlet

The internal harness compares the engine to *itself*. That is the right tool for "is this
change an improvement" and the wrong one for "how strong is this" — an engine judges
improvements by the standards of the evaluation it already has, and it cannot see a weakness
both sides share. For that you need an opponent nobody here wrote.

### Setup

You need a match runner and a reference engine. Neither is vendored; see `tools/README.md`
for where to get them and put them in `tools/`, or anywhere on `PATH`.

```bash
cd tests
make gauntlet          # builds the UCI adapter and the book generator
./gauntlet.py --which  # reports what it found, with versions
```

`--which` should show a match runner, Stockfish, the built engine and the opening book.
**Record the versions it prints alongside any figure you quote.** A different major version
of Stockfish will move every number in `doc/strength.md`.

### The UCI adapter

The engine draws its own board on a Commodore 64 and has no idea what a tournament is.
`make uci` builds a desktop-only translator that speaks UCI, the protocol every chess GUI and
match runner uses, so the engine can be played by any of them or examined in an analysis
tool.

Two things about it are deliberate:

- **It ignores every clock the referee sends.** Strength here is a node budget, not seconds,
  and that is what makes a result reproducible on any host. Limit the *opponent* by nodes or
  depth too, or you have thrown that away on one side of the board.
- **It is built without `-DEVAL_TUNING`.** That switch exists for the tuning harness and makes
  every node dearer. The thing being measured has to be the thing that ships.

Node budgets are clamped to 65535, because that is what a 16-bit counter holds on the target.
Asking for more would measure a configuration no C64 can reach.

### The opening book

The search is deterministic: from one position it plays one game, so a match without varied
openings is one game repeated N times. `tests/book.epd` is 256 positions reached by a few
random legal non-capturing moves — varied, balanced, and generated from fixed seeds rather
than typed out.

**It is checked in, and it is not a build dependency.** Every published figure was measured
against that exact file. `make book` regenerates it, and you should expect to re-measure
everything if you do.

### Running it

```bash
./gauntlet.py --games 512                        # the ladder
./gauntlet.py --games 512 --anchor               # ...and the rated rung
./gauntlet.py --levels 4 --nodes 100 --games 128 # one rung
./gauntlet.py --games 512 --pgn-dir /tmp/pgn     # keep the games
```

Each skill level plays a ladder of Stockfish settings, and scores become rating differences
with 95% intervals. `--anchor` adds a rung against Stockfish's rating-limited mode, which is
the only thing that turns a ladder into a number — and the only thing here that uses a clock
and is therefore not reproducible. `doc/strength.md` §4.3 explains why that rung deserves
more suspicion than its error bar suggests.

---

## 5. Reproducing the strength figures

```bash
cd tests
make gauntlet
./gauntlet.py --which                                  # record these versions
./gauntlet.py --games 512 --levels 1,2,3,4 --nodes 1,30,100,300
```

**Run the self-play validation first and do not proceed if it is not level.** It is the step
that catches a broken harness before it produces a convincing wrong answer:

```bash
./chesstest match sanity
```

The ladder is deterministic on this engine's side, so you should reproduce
`doc/strength.md` Appendix A rather than merely come close. Stockfish at a fixed node count
is deterministic too, so differences of more than a game or two mean something changed.

If you have both runners, run it twice — `--cli` picks one explicitly. Two independent
implementations agreeing is how the published table was checked, and it is what caught the
one real mistake in the whole exercise: a result parser that silently reported *normalized*
Elo, producing a table with the right signs, the right ordering, and every number on the
wrong scale.

---

## 6. On-target measurement

**Everything above runs on a desktop, which is not the machine this engine is for.** A change
that is free on a modern CPU can be expensive on a 6502, and the native suite cannot see it.

```bash
cd tests
cl65 -t c64 -Oris -I../src -o c64search.prg \
    ../src/engine.c ../src/eval.c ../src/search.c c64search.c
./vice-run.sh c64search.prg /tmp/out.png 400000000
```

| Program | Measures |
|---|---|
| `c64perft.c` | Raw move generation |
| `c64search.c` | Search cost at three fixed budgets |
| `c64level1.c` | Per-move time through a real game at the easiest setting |
| `c64skill.c` | Per-move time and depth reached, per skill level, over 20 plies |

**The profile in `doc/rework-log.md` was taken with a program that is not in the tree** — it
needed a modified engine as well as a driver, so it was done on a scratch fork and not kept.
The technique is worth restating, because it is cheap and it is what overturned the last
major optimisation on the plan.

There is no profiler for a 6502 here and there does not need to be one. Run the search
normally, then again with **one component doing an extra redundant copy of its work**, and
the difference is that component's cost *in situ* — including the call overhead a
source-level model would miss. Every doubled operation must be either side-effect free (a
second attack test whose result is discarded) or exactly self-reversing (a make immediately
followed by an unmake). Identical node counts across runs prove the tree was untouched.

That measurement said move generation was 42% of search time and legality only 14% — the
exact inverse of what had been assumed from perft numbers — and a planned optimisation was
dropped on the strength of it.

`vice-run.sh` documents three things that each took a while to work out — VICE finds its ROMs
through a `./data` symlink and ignores `VICE_DATADIR`; the macOS build needs GTK environment
variables set by hand; and `-warp` does **not** distort a measurement, because the jiffy clock
counts emulated time. Use `-ntsc`, since cc65's `CLOCKS_PER_SEC` of 60 is only true there.

### Two traps specific to the target

**The native suite validates logic, never machine width.** cc65's `int` is 16 bits and the
host's is 32. A node guard written as `(n + n + n) > budget` wrapped above 21845 and never
fired at all at level 4 — the whole suite, the ladder and the match harness passed either
way. Anything that multiplies, accumulates or sums toward a limit needs reading by hand, or
checking with a throwaway `cl65` program.

**Do not measure from the opening position.** It is the optimistic case, not the conservative
one: after eight moves nothing has been traded and the lines are open, so the generator has
more to do and quiescence has more captures to chase. Measured, 45 nodes/sec in the opening
against 35 in the middlegame. Every budget derived from the opening is about a quarter
optimistic.

---

## 7. Is it still playable?

Everything above talks to the engine directly and never touches the game. `driveterm.py` runs
the curses build under a pty and reads the screen back, with canned scripts for startup,
AI-vs-AI, the attack overlay, and a human move followed by an undo.

```bash
cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c src/search.c \
   src/board.c src/undo.c src/cpu.c src/human.c src/frontend.c src/main.c \
   src/term/platTerm.c -o /tmp/chessterm

python3 tests/driveterm.py            # start up, show the first screen
python3 tests/driveterm.py aivsai
python3 tests/driveterm.py humanmove  # e2-e4 as white, then undo
```

The unit tests cover the engine; this covers whether the game is a game. Read it as evidence
that things are being drawn, not as a pixel-accurate reference — partial redraws can leave
stale glyphs in its terminal model that a real terminal would not show.

---

## 8. The workflow

For a change intended to make the engine stronger:

1. `make test` — green, always, before anything else is worth discussing.
2. `./chesstest match <comparison>` at 512 games, against the version without the change.
3. If it wins at equal nodes, measure its cost on a C64 and re-run at **equal time**. This is
   the step that reverses answers.
4. If it survives that, `./gauntlet.py` against an external opponent — self-play cannot see a
   weakness both sides share.
5. Check the arithmetic against a 16-bit `int` by hand.

For a change intended to make it faster, the bar is different and higher: **behaviour should
be bit-identical.** The incremental evaluation rewrite was accepted partly because the
512-game match returned exactly the same 113-113-286 and self-play reported the same mates on
the same plies. A pure speed change that alters a single game has a bug in it — and when one
legitimately does change behaviour, as the capture-only generator did, that difference needs
its own explanation before the speed is banked.

---

## 9. What none of this covers

**Four of the six 8-bit targets have never been run.** `c64.chr`, `apple2`, `atmos` and
`plus4` compile clean and link inside their budgets, and no platform file was edited, so they
*should* be fine. That is an argument, not evidence, and it is the largest untested surface in
the project.

**`atari` and `cx16` do not build here** and had pre-existing failures unrelated to the engine
work.

**There is no continuous integration.** `make test` is a command someone has to remember.
