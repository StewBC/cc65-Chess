# cc65-Chess versus Sargon II

This directory contains the repeatable match harness and the operational notes
for playing the native cc65-Chess engine against Apple II Sargon II under
`a2m-v2`.  The emulator stays visible and runs at maximum turbo so a person can
follow the games.

Nothing in this workflow modifies the source Sargon disk.  Match artifacts go
under `scratch/`, and the harness is resumable after an interruption or bridge
failure.

## Files

- `match.py` drives Sargon through the a2m-v2 control port, drives the exact
  cc65-Chess engine through `tests/uci`, referees games with python-chess, and
  writes CSV, PGN, JSON summaries, and failure logs.
- This file is the runbook.  Update it whenever a new Sargon display or
  emulator behavior is discovered.

## Required local layout

The harness derives these paths from the cc65-Chess repository root:

```text
cc65-Chess/
  sargon/match.py
  sargon/Sargon-trimmed.dsk
  tests/uci
../a2m-v2/
  build/a2m-v2
  tools/a2m_control_client.py
```

### The disk image is not in this repository

Sargon II is a commercial program from 1978 and its disk image is nobody's here
to redistribute, so `.gitignore` excludes `sargon/*.dsk` while tracking the rest
of this directory.  Supply your own copy at:

```text
sargon/Sargon-trimmed.dsk
```

It must be exactly **143,360 bytes** - a standard DOS 3.3 image - with SHA-256:

```text
f0ad0bd2c4b162f5a410f4b2ddb4e6a0e70e4a0bbadd247c604f80e0cd47e1b8
```

```sh
shasum -a 256 sargon/Sargon-trimmed.dsk
```

Two things that image is not.  The commonly circulated `Sargon.dsk` is 153,394
bytes: the same 143,360-byte image followed by a 10,034-byte trailer, and a2m-v2
will not boot the oversized raw file - trimming it to the first 143,360 bytes
produces the hash above.  And do **not** substitute the protected
`Sargon II.nib`; that image was observed announcing a false mate from the
initial position.

`match.py` validates the size, copies the file to
`/private/tmp/cc65-sargon-match.dsk`, and mounts that disposable working copy,
so nothing in this workflow can write to your original.

Python needs the `chess` package.  Build the native UCI adapter before a run:

```sh
make -C tests -B uci
```

The repository test suite can be rebuilt and run with:

```sh
make -C tests test
```

## Exact Sargon boot choreography

The compilation disk is not directly bootable into the game.  The reliable
sequence is:

1. Launch a2m-v2 with the trimmed disk and a control port.  Do not use
   `--headless`.
2. Wait for the `SARGON ][` catalog/menu screen.
3. Send uppercase `R`, then uppercase `H`, as individual keys.  Do not send
   Return with either key.
4. Ignore the transient `INSERT COPY DISK & HIT RETURN` screen.  It clears by
   itself.  Pressing Return there drops into the monitor.
5. Wait for the `NEW GAME` prompt.
6. Send `G` followed by Return.
7. At `YOUR COLOR`, send `W` or `B` followed by Return.
8. At `LEVEL OF PLAY`, send `0` through `6` followed by Return.

`R` and `H` must be uppercase.  The distinction between the no-Return loader
keys and the later choice-plus-Return prompts is important.

After boot, send the control command `set-turbo max`.  a2m-v2 does run at max,
but its window title currently does not update when speed is changed through
the control port.

## Reset behavior

The a2m-v2 `reset` control command returns this Sargon disk to the reusable
`NEW GAME` state.  This was verified both textually and graphically:

- initial board frame SHA-256:
  `c8a566b8c37bddd5bdb5d23add6aefa2113cecc86753d58f86ae8d8c1a688f20`
- after `E2-E4 C7-C5`:
  `2275ee882c95532bcab053fbb194490ef8e9dd13a286cde2fe9d09a87e0e1d04`
- after reset and starting another game, the frame returned byte-for-byte to
  the initial hash.

Apple II snapshots are exposed by a2m-v2 but currently time out and are not
usable.  Reset is therefore the per-game restoration mechanism.

## cc65-Chess side

`tests/uci` uses the same engine and four shipped skill budgets as the Apple II
game:

| UCI skill | Menu name | Nodes |
|---:|---|---:|
| 1 | Very Easy | 400 |
| 2 | Easy | 1,200 |
| 3 | Harder | 15,000 |
| 4 | Very Hard | 60,000 |

The shipping 8-bit front end bypasses normal search for the first move of a
game - four entries as White, and five entries of two replies each as Black.

`match.py` used to reproduce the White table in Python, because the adapter
called the search directly and could not reach `cpu.c` at all.  It no longer
does: `tests/uci` has `OwnBook` and `BookSeed`, `UCIEngine` sets both, and the
harness asks the engine what it plays.  `--no-own-book` turns the tables off,
which is the pre-2026 behaviour and the control for anything measured with them
on.

## Sargon opening randomization

A scripted reset otherwise repeats Sargon's opening choice.  Sargon's keyboard
wait loop increments zero-page `$4E`, which supplies its opening entropy.  At
the level prompt the harness:

1. pauses the emulator;
2. writes a deterministic byte to `$004E`;
3. queues the level and Return while paused;
4. resumes execution.

Color-swapped game pairs receive the same seed, and consecutive pairs advance
the low bits because Sargon's picker consumes only a few of them.  The exact
seed is recorded implicitly by the batch name, game index, and retry number in
`seed_for()`.

cc65's own opening variety is small and bounded: four first moves as White, and
two replies to each of five White first moves as Black.  Do not treat a handful
of repeated cc65 openers as a bridge failure - the rest of the diversity comes
from Sargon's book, Sargon's colour, and later play.  Summaries count distinct
six-ply prefixes, and `doc/strength.md` §5.1.7 is why that count matters more
than the score.

## Text screen layout and move parsing

The Apple II text page is read from `$0400-$07ff` and decoded using the normal
24-row interleave.  Sargon's move table has fixed columns:

- move number in the first ten columns;
- Player/White cell at columns 10-17;
- Sargon/Black cell at columns 23-30.

Which column belongs to Sargon depends on the selected color.  Legal move
forms observed so far are:

```text
E2-E4       ordinary move
F5XD3       capture
0-0         king-side castle
0-0-0       queen-side castle
PXPEP       en passant
```

Coordinates are translated against python-chess's current legal move list,
which also resolves promotion to a queen.  `CHECK` is a nonterminal annotation
after a coordinate move.  `CHECKMATE` and `STALEMATE` are terminal displays;
on those screens Sargon may not put the normal player cursor in its usual
column.

### `MATE IN n` is status, not a move

Sargon can display `MATE IN 1` (and similar forecasts) for a long time while
searching.  It can also put the forecast in the next move row after its real
coordinate move, for example:

```text
52  ...  C1-D2
53  `    MATE IN 1
```

The forecast must never be fed to the referee as a move.  When the input
cursor has returned, the bridge compares the new table with the pre-move
snapshot and selects the newest newly-added valid coordinate token, ignoring
later status cells.  Do not press a key merely because `MATE IN n` persists;
some Level 1 searches have taken more than a minute at emulator max.

### Move 100 is printed as `:0`

Sargon has only a two-character move-number field and does not implement a
hundreds digit.  It advances the tens character through ASCII instead:

```text
99, :0 (100), :1, ... ;0 (110), ... ?0 (150)
```

The parser decodes `0` through `?` as digit values 0 through 15.  `?0` is full
move 150, matching the harness's 300-ply safety cap.  If this rollover is not
recognized, the bridge cannot see the player cursor at move 100 and appears to
hang.

## Referee and game policy

python-chess is an independent referee.  It validates every coordinate move,
maintains the authoritative position, and determines game outcomes.  Draw
claims are enabled, including threefold repetition and the fifty-move rule.
Games also have a 300-ply safety cap.  Colors alternate every game.

Every completed game is appended immediately, so Ctrl-C is safe.  Rerunning
the same command resumes at the next CSV index.  A failed game is retried up to
three times; the retry changes its seed so one broken position cannot wedge an
entire batch.  Failures are retained rather than silently discarded.

Each batch writes:

```text
<batch>.csv
<batch>.pgn
<batch>-summary.json
<batch>-failures.log     # only when a failure occurred
```

The CSV contains the full UCI move list as well as result, cc65 color, draw or
mate reason, ply count, duration, and six-ply opening prefix.  The PGN is the
best starting point for reproducing suspected cc65 late-endgame defects.

## Running screens and matches

Use a scratch output directory so generated games remain outside the shipped
source tree:

```sh
python3 sargon/match.py \
  --only calibration \
  --calibration-games 64 \
  --cc-skill 2 \
  --output scratch/sargon-vs-cc65-YYYYMMDD
```

The emulator window remains visible.  Sargon defaults to L1 for calibration.
To run a 512-game measured match explicitly:

```sh
python3 sargon/match.py \
  --only match \
  --match-games 512 \
  --cc-skill 2 \
  --sargon-level 1 \
  --output scratch/sargon-vs-cc65-YYYYMMDD
```

A four-game bridge smoke test is:

```sh
python3 sargon/match.py \
  --only smoke \
  --smoke-games 4 \
  --cc-skill 1 \
  --sargon-level 1 \
  --output scratch/sargon-smoke-YYYYMMDD
```

Do not run more than one harness on the default control port 6511.  A second
read-only control client may also block because the active harness polls the
same emulator continuously.

## Calibration policy used for the first study

The initial idea was Very Easy versus Sargon L1, raising Sargon only if cc65
dominated.  The first screen showed the mismatch was in the opposite
direction: Very Easy scored 0 wins, 7 losses, and 5 draws (20.8%) in 12 games.

The revised ladder keeps Sargon at L1 and raises cc65 through the shipped
levels:

1. Very Easy, 400 nodes;
2. Easy, 1,200 nodes;
3. Harder, 15,000 nodes;
4. Very Hard, 60,000 nodes.

Run 64-game screens.  A score below 35% advances to the next cc65 tier.  A
score in the 35%-65% band is a candidate for the 512-game measured match.  If
a tier jumps above the band, compare it with the previous tier and use the one
closest to 50%.  If Very Hard is still clearly below Sargon L1, report that;
Sargon L0 is a separate experiment rather than silently changing the
question.

The Easy screen immediately produced much longer endings than Very Easy,
including fifty-move draws, insufficient-material draws, and games beyond
move 100.  These retained games may expose a cc65 late-game defect and should
be mined as regression positions after the match data is collected.

*They did, and they were.*  The defect was that nothing in the evaluation
preferred one bare-king position to another, so a won ending had no gradient to
climb.  Game 0 of the baseline is now `tests/c64drive.c`, replayed on a real
C64 to price the fix; the king-and-rook position from move 66 is the first
entry in `chesstest convert`.

### Completed pre-fix baseline (2026-08-10)

The Easy-versus-Sargon-L1 screen was deliberately stopped at 64 games because
a cc65-Chess late-game fix was already being prepared.  The completed baseline
is:

```text
cc65 Easy: 10 wins, 19 losses, 35 draws
score:     27.5 / 64 = 42.96875%
openings:  17 distinct six-ply prefixes
plies:     8,207 total, 128.23 mean, 230 maximum
```

By cc65 color:

```text
White: 9 wins, 3 losses, 20 draws
Black: 1 win, 16 losses, 15 draws
```

Terminations:

```text
checkmate                 29
threefold repetition      18
fifty-move rule           15
insufficient material      2
```

The large color split is important and must be retained when evaluating the
fix.  The result artifacts are:

```text
scratch/sargon-vs-cc65-20260809/calibration-cc2-sargon1.csv
scratch/sargon-vs-cc65-20260809/calibration-cc2-sargon1.pgn
scratch/sargon-vs-cc65-20260809/calibration-cc2-sargon1-summary.json
scratch/sargon-vs-cc65-20260809/calibration-cc2-sargon1-failures.log
```

All 64 CSV rows are present and all 64 PGNs parse without errors.  The one
failure-log entry is the bridge discovery where row 52 contained Sargon's real
`C1-D2` move and row 53 contained `MATE IN 1`; the game was retried and the
newest-valid-coordinate selection fix was applied before the remaining run.

As a rough unpaired summary, 42.97% corresponds to about -49 Elo and a normal
95% score interval of about 34.8%-51.1%.  This is not a publishable strength
estimate: the engine is deterministic, the opening set is small and repeated,
64 games is a screen rather than the prescribed 512, and the suspected cc65
late-game defect means this run is intentionally a pre-fix baseline.

### Completed post-fix run (2026-08-10)

The late-game fix landed as two commits — the mate drive itself, and then a
second gate on it that this rig is the reason for.  The same screen was re-run
at the same settings, 64 games, cc65 Easy versus Sargon L1:

```text
cc65 Easy: 27 wins, 20 losses, 17 draws
score:     35.5 / 64 = 55.47%     (pre-fix 27.5 / 64 = 42.97%)
openings:  16 distinct six-ply prefixes
plies:     8,028 total, 125.4 mean, 193 maximum
```

By cc65 color, which is where the change is visible:

```text
White: 25 wins,  2 losses,  5 draws   (pre-fix  9W  3L 20D)
Black:  2 wins, 18 losses, 12 draws   (pre-fix  1W 16L 15D)
```

Terminations:

```text
checkmate                 47     (pre-fix 29)
threefold repetition      13     (pre-fix 18)
fifty-move rule            2     (pre-fix 15)
insufficient material      2     (pre-fix  2)
```

And on the measure the fix was for — a clear piece up for ten plies or more:

```text
converted:              26 of 28  (92%)     (pre-fix 6 of 23, 26%)
drew still a piece up:   0                  (pre-fix 15)
gave the material back:  2                  (pre-fix 2)
```

The fifteen games that ended still holding a rook against a bare king are gone.

**Do not quote the percentage on its own.** Naive 95% intervals are 30.8%-55.1%
and 43.3%-67.6% and they overlap; the effective sample is 16-17 distinct games,
not 64.  The conversion count and the fifty-move column are what carry the
result, because they are categorical outcomes tied to the mechanism.

### What this rig found that nothing in the repository could

The first version of the fix passed every test in `tests/`, and scored *better*
than the shipped version on the internal self-play conversion metric — 85%
against 81%.  Played here it took 51.6% to 31.2% over 32 games.  Its gate was
loose enough to change moves in middlegames, and no internal instrument could
see that: the endgame tests contain only bare-king positions, and in self-play
both sides carry the same harm so it cancels.

That is the third time a defect in this engine has been found by playing rather
than by measuring, and it is the argument for keeping this rig alive.

### Two properties of this rig that the study has to live with

**Sargon is not reproducible.**  The `$4E` seed is written before the level is
typed, but the keyboard-wait counter keeps advancing afterwards, so book choices
depend on host timing.  The same seed opened `1.d4 Nf6` in one run and
`1.d4 d5` in the next with cc65's own moves identical.  Paired replay of a game
index against an earlier run is therefore impossible; all comparisons here are
unpaired.

**64 games is not 64 samples.**  The pre-fix baseline holds 17 distinct move
lists and the post-fix run 16; on the Black side it is four or five.  The
fifteen pre-fix fifty-move draws were one game played fifteen times.  Scores
from this rig move in large steps and should be read that way.

**cc65 had an opening book only as White**, four entries deep, while Sargon has
one on both sides.  That was written here as the most likely cause of the
sixty-point gap between cc65's White score (85.9%) and its Black score (25.0%).

It was checked, and it is wrong in its mechanism.  `doc/strength.md` §5.1.7 has
the whole of it; the short version is that all eighteen Black losses came from
two openings, the fourteen `1.e4 Nc6` games were **one identical 103-ply game**,
and outside those two lines Black scored 57% and did not lose.  Stockfish says
neither losing game was lost in the opening — the `1.e4` game is winning for
Black on move 13 and thrown on move 17.  Played from `tests/book.epd` where no
book fires for either side, the engine scores the same with both colours.

The gap is a **weighting**, not a strength difference: White's four-entry table
gave 32 games twelve distinct openings, and Black's absence of one gave 32 games
five.  A book helps by spreading the sample, and one bad game counted fourteen
times is what 25.0% mostly is.

Black now has a reply table too, and this rig is what confirms it.

### Two things about this harness changed with that

**The opening tables are the engine's, not this script's.**  `match.py` used to
carry a Python copy of the White table, because `tests/uci` called the search
directly and could not reach `cpu.c` at all.  The adapter now has `OwnBook` and
`BookSeed`, `UCIEngine` sets both, and the harness asks the engine what it plays.
A copy of a table is a copy of what it was believed to contain, which is exactly
the thing a rig for finding wrong beliefs should not have.

**`--cc-color` pins the colour for a whole run.**  A colour split is measured
with half the games, and the Black side is where the open questions are: 32
alternating games gave five distinct Black openings.  `--cc-color black` spends
all 64 on the side being asked about, and the per-game seed advances every game
rather than every pair, because there is no colour-swapped partner to hold it
constant for.

```sh
python3 sargon/match.py \
  --only calibration --calibration-games 64 \
  --cc-skill 2 --cc-color black \
  --output scratch/sargon-blackbook-YYYYMMDD
```

`--no-own-book` turns the tables off, which is the pre-2026 behaviour and the
control for anything measured with them on.

## Sargon's deeper levels, and what they cost

Everything measured in `doc/strength.md` is against **level 1**.  Level 6 has
been played once, six games, and the operational numbers are:

```text
~45 seconds per Sargon move at a2m-v2 max turbo
84 minutes a mean game, 8.4 hours for six
```

Two practical consequences.

**`MOVE_TIMEOUT` is not enough at the deep levels.**  The default is 120s per
Sargon move; a search that overruns it is scored as a bridge failure and the
game is retried rather than waited for, which loses the game rather than the
move.  `--move-timeout` raises it, and 1200 was used for level 6.

**Estimate from the whole game, not from move four.**  Sargon's cost tracks the
piece count, so an early move is the expensive case and endgames are cheap.
Extrapolating a level-6 move-four time across a hundred moves predicted sixty
hours for six games; it took eight.

### The band nobody has measured

Levels 2 to 5 have never been played.  cc65 Easy against level 1 sits at 55%,
and cc65 Very Hard against level 6 went 2W 3L 1D over six games - so the
competitive band for the *upper* cc65 levels is somewhere in that gap and is
the obvious place to point this rig next.  The calibration policy above
(64-game screens, 35%-65% band) is the procedure; the gate is knowing how slow
each level is, which is a handful of moves per level to establish and has not
been done.

## Reading the games afterwards

`analyse.py` runs Stockfish over a PGN and prints, per game, every move that
moved the evaluation by more than a threshold, attributed to the side that
played it:

```sh
python3 sargon/analyse.py scratch/<run>/smoke.pgn --depth 18 --engine cc65
```

It is what found that cc65 reaches move 13 *winning* in the games it loses to
level 1 (`doc/strength.md` §5.1.7) and *behind* in every game against level 6.

Three cautions, all of them earned:

- **Stockfish judges by a standard neither program can play to.**  A position
  it calls equal can be lost for a 60,000-node engine.  Sargon's `11.Nxf7` in
  the §5.1.7 game is worth −139 to Sargon and won the game anyway.
- **Mate scores clamp at ±10000**, so walking into a forced mate and failing to
  deliver one both print as ~9000cp swings.  They are opposite errors and the
  blunder counts do not tell them apart.
- **A per-game count is not a per-move cost.**  Counting cc65's queen moves per
  game correlated beautifully with the result and was close to meaningless,
  because a losing position invites queen moves.  Cost per move, split by game
  phase, said something different and better - `doc/strength.md` §5.2a has the
  whole sequence.

## Known a2m-v2 limitations and cautions

- Do not use `--headless`; the visible window is part of this study.
- `set-turbo max` works, but the window title does not reflect the control-port
  speed change.
- Apple II snapshots currently time out.  Use verified reset instead.
- Do not send Return during the `R`, `H` boot-loader sequence.
- Do not type into the emulator window while automation owns focus; host
  keystrokes can be consumed by the Apple II.
- Do not assume `MATE IN n` means the program is finished.
- Do not assume decimal-only move numbers after move 99.
