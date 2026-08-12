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
- `levelcost.py` answers "how slow is each Sargon level" in six minutes, which
  is the question that decides whether a match at some level is an afternoon or
  a fortnight.  It shares the boot choreography, the control port and the
  one-harness-at-a-time rule with `match.py`.
- `analyse.py` runs Stockfish over a finished PGN and attributes every large
  evaluation swing to the side that played it.
- `piececost.py` answers "what does moving each kind of piece cost, per move,
  by phase" - the third statistic in `doc/strength.md` §5.2a, and the one that
  found the first two were measuring something else.
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

The copy these results were produced with came from:

```text
https://www.myabandonware.com/game/sargon-ii-54m#Apple%20II
```

Recorded as provenance, not as a recommendation or a guarantee - check the hash
above rather than trusting any particular download, since what is served under
one name changes over time.

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
| 3 | Harder | 18,000 |
| 4 | Very Hard | 65,000 |

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

**`--only calibration` plays Sargon level 1 whatever `--sargon-level` says.**
The calibration phase is the fixed screen from the original study - level 1,
raise cc65 - and it hard-codes the level and the batch name to match.  Any
other level is `--only match --sargon-level N --match-games N`, which is also
what names the output `match-ccX-sargonY` instead of `calibration-...`.

A colour-split screen is two runs, and they want **separate output
directories**: `--cc-color` does not reach the batch name, so both halves would
otherwise resume into one CSV and the second half would start at the first
half's game index and inherit its seeds.

```sh
python3 sargon/match.py --only match --match-games 32 \
  --cc-skill 3 --sargon-level 3 --cc-color white --move-timeout 300 \
  --output scratch/sargon-l3-cc3-white-YYYYMMDD
```

And the cost of any of this before starting it:

```sh
python3 sargon/levelcost.py --output scratch/sargon-levelcost-YYYYMMDD
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
3. Harder, 18,000 nodes;
4. Very Hard, 65,000 nodes.

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

Every level has now been timed.  `levelcost.py` plays the opening of one
throwaway game per level and times Sargon's replies; all seven levels take six
minutes of emulator time.

```text
level   opening replies (s)      median   per Sargon move   64 games   512 games
  0     0.1  0.1  0.1  0.1         0.07      ~0.3 s           20 min     2.5 h
  1     0.3  0.3  0.4  0.4         0.34       0.6 s *         40 min       5 h
  2     0.3  0.4  0.5  0.8         0.42      ~0.7 s           45 min     6.0 h
  3     1.0  1.3  1.8  2.6         1.54       1.50 s *         1.6 h      13 h
  4     4.2  4.8  5.4              4.79       3.79 s *         4.3 h      34 h
  5     7.3  8.9 16.5              8.95      7 to 17 s      8 to 18 h    65-145 h
  6    44.7 48.6 191.0            48.59      90.7 s *          93 h    31 days
```

`*` measured over whole games: the 64-game level-1 screen, the 64-game level-3
screen below, the 16-game level-4 probe below that, and the six-game level-6
run.  Level 5 is a range on purpose, and the reason is the useful part.

**A one-factor model fitted two anchors and was wrong in the middle.**  With
only levels 1 and 6 measured, one constant - the median opening reply times
1.80 - reproduced both to within 4% across a range spanning a factor of 150.
It predicted 2.8 s a move at level 3.  The level-3 screen then measured
**1.50**, so the tidy model over-predicted by 87% at the one point between its
two anchors.  Two anchors and a straight line through them is not a model, and
this one agreed at both ends for different reasons at each end.

What the three measured levels actually say about the probe:

```text
level 1    probe 0.34   whole game 0.6     1.76x - but see the overhead below
level 3    probe 1.54   whole game 1.50    0.97x
level 4    probe 4.79   whole game 3.79    0.79x
level 6    probe 48.6   whole game 90.7    1.87x
```

So **the opening is representative through level 4 - slightly pessimistic, if
anything - and only half the story at level 6**, where the direction flips.
Use the probe as-is up to level 4, expect up to double it at 5 and 6, and check
the first few games of any long run against the estimate rather than trusting
it.  Every estimate made here before it was measured came out **high**, twice
by a lot, which is the safe direction but not an accurate one.

The band the study needed is levels 2 to 5, and the shape of it is that **the
wall between an afternoon and a fortnight sits between level 4 and level 6** -
level 3 is under two hours for 64 games, level 4 an afternoon, and level 6 four
days.

Three things this corrected, two of them numbers that were written down here.

**"45 seconds per Sargon move" was seconds per *ply*.**  The level-6 run is
8.39 hours over 666 plies, and only half of those plies are Sargon's; cc65
answers in 8 ms at Very Hard on this host, measured.  Per Sargon move it is
90.7 seconds, 73 to 137 across the six games.  The 84-minutes-a-game figure was
right and this one was out by two, which matters because the per-move number is
the one that gets multiplied when a match is being sized.

**"An early move is the expensive case" holds at the shallow levels and
inverts at the deepest.**  This file used to state it flatly, off Sargon's cost
tracking the piece count.  At level 6 a whole game costs 1.9x its opening per
move, which is the opposite; at level 3 the opening is if anything slightly
dearer, which is the original claim.  Neither is wrong, and the flip is
somewhere between - deep searches in complicated middlegames are where level
6's time goes, and that is not a piece-count story.

Read the cheap end carefully, because at level 1 this harness is a large part
of its own measurement: `player_move` types six keys at `KEY_DELAY`, which is
0.15s before Sargon has thought at all, and the screen poll adds another 25ms.
That is arithmetic off the constants rather than a measurement, and it is
nearly a third of the level-1 figure against 0.2% of the level-6 one.  Anything
inferred from the ratio between a probe and a game mean at the cheap levels is
partly measuring the bridge.

**And the host matters, which is worth 11%.**  The level-3 screen ran its White
half while a 6-core Stockfish gauntlet had the machine and its Black half with
the machine mostly idle: 1.67 s a move against 1.50.  Neither result changed -
both programs are node limited, which is the whole reason this rig is allowed
to share a host - but a timing figure taken under load is 11% pessimistic.

**What actually broke the sixty-hour prediction was variance, not phase.**  One
level-6 move in three took 191 seconds against a 48.6-second median for its own
level - 4x.  That is the number to size `MOVE_TIMEOUT` against, because a
search that overruns it is scored as a bridge failure and the game is retried
rather than waited for, which loses the game rather than the move.  Allow about
20x the projected per-move cost: the 120s default is ample through level 3, 300
at level 4, 600 at level 5, and 1200 was used for level 6.

### Completed level-3 screen (2026-08-11)

The first use of the numbers above.  cc65 **Harder** against Sargon **level
3**, 64 games, run as two single-colour halves of 32 so the colour split is
measured rather than halved:

```text
cc65 Harder:  42 wins, 12 losses, 10 draws
score:        47.0 / 64 = 73.4%
openings:     27 distinct six-ply prefixes, most-repeated game 6x
plies:        118 mean
cost:         1.6 hours for the 64 games
```

By colour, and terminations:

```text
White: 26W  4L  2D = 84.4%   12 distinct in 32
Black: 16W  8L  8D = 62.5%   15 distinct in 32

checkmate 54, threefold 8, fifty-move 1, stalemate 1
```

**Two of these are the best numbers this rig has produced and neither is the
score.**  27 distinct games in 64, against 16 to 17 in the level-1 study, with
the worst repeat down from fourteen to six - so for the first time the
effective sample is most of the games played.  And **one** fifty-move draw
where the pre-fix baseline had fifteen, which is the endgame work holding at a
level it had never been played at.  Black at 62.5% against 45.3% at level 1 is
the reply table and the deeper search together.

**73.4% is above the 35%-65% band, so level 3 is not the pairing**, and by the
policy the next screen is level 4.

### Completed level-4 screen (2026-08-11)

The sixteen-game cost probe was resumed in the same output directory to the
full 64 games, with cc65 **Harder** against Sargon **level 4**:

```text
cc65 Harder:  22 wins, 27 losses, 15 draws
score:        29.5 / 64 = 46.1%
openings:     28 distinct six-ply prefixes, most-repeated game 6x
plies:        121 mean
cost:         4.27 hours, 4.00 minutes a game
```

By colour, and terminations:

```text
White: 17W 12L  3D = 57.8%   17 distinct in 32, worst repeat 5x
Black:  5W 15L 12D = 34.4%   11 distinct in 32, worst repeat 6x

checkmate 49, threefold 15, fifty-move 0, stalemate 0
```

**Level 4 is the calibrated pairing.**  The aggregate 46.1% is inside the
35%-65% competitive band, and 28 distinct openings is marginally better than
level 3's 27.  More important for future candidate checks, zero fifty-move
draws says the endgame work still holds at this new level.

The colour split is also part of the baseline, not something to average away.
Black is half a point below the nominal band and draws twelve games; a future
candidate must not turn that existing weakness into a categorical collapse.
Comparisons remain unpaired because Sargon is not reproducible, so the score's
honest interval is still roughly +-12 points.  What this screen buys is an
outside opponent at the right aggregate strength and baseline categories to
compare: colour, distinct games, fifty-move draws and termination causes.

### The levels do not sit on one scale, and this is where that shows

Before the screen the levels were placed by interpolating between cc65 Easy at
55.5% against level 1 and cc65 Very Hard at 41.7% against level 6: about 123
rating points a level, which the timing appeared to corroborate at 2.7x more
time per level and `doc/strength.md` §4.2's 60 points a doubling.  **That
predicted 58% for this screen.  The answer was 73.4%.**

The two ends will not reconcile, either.  Reading down from the level-3 result
puts the levels about 44 points apart and level 6 near 1615 - at which cc65
Very Hard, rated 1946, should have scored about 90% there instead of 41.7%.
Six games cannot carry that weight; 64 games at level 3 cannot be waved away
either.

The honest reading is §4.3's non-transitivity arriving again: **Sargon's levels
are not a ladder that can be interpolated, and cost per level is not strength
per level.**  Level 6 costs sixty times level 3 in time and is plainly not
sixty times anything in strength.  The band has to be walked one 64-game screen
at a time, and the cost table is what says which rungs can be afforded.

### What that means for a match

A 64-game screen costs 40 minutes at level 1, **1.6 hours at level 3**, **4.3
hours at level 4** and 93 at level 6.  A 512-game measured match costs 13 hours
at level 3 and four to six days at level 5.

**Read the calibration policy's "advance to a 512-game measured match" as
retired above level 3 rather than inherited.**  At 64 games this rig now yields
28 distinct games; at 512 it would yield perhaps 120, for a fortnight, at the
levels that are actually competitive.  The statistics come from Stockfish and
the confirmation comes from Sargon; `doc/strength.md` §5.1.7 is where a
Stockfish ranking failed to transfer here on both of the measures tried, and
§5.2b is the first time that order ran in the cheap direction - a candidate
closed by forty minutes of Stockfish never cost this rig a game.

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
- **And what that clamp does to an average is worse.**  This caution used to
  stop at blunder counts.  Over 21 games, five clamped moves in 231 turned the
  mean cost of a middlegame pawn move from -27.9 into **+166.1** - a pawn move
  reading as a large *gain* - and two in 94 turned an opening knight move from
  -23.3 into -226.7.  Any mean over evaluation deltas needs the mate scores
  dropped and a median printed beside it, which is what `piececost.py` does and
  what `doc/strength.md` §5.2a did not.  That table is now believed to be
  mostly this artifact; §5.2b has the replication.
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
