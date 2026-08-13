# cc65-Chess versus Sargon II

This directory contains the match harness and the operational notes for playing
the native cc65-Chess engine against Apple II Sargon II under `a2m-v2`. The
emulator stays visible and runs at maximum turbo so a person can follow the
games.

Nothing in this workflow modifies the source Sargon disk. Match artifacts go
under `scratch/`, and the harness is resumable after an interruption or bridge
failure.

The current HEAD result is in `doc/strength.md` §4.5. This file is how to run
the harness, not a results archive.

## Files

- `match.py` drives Sargon through the a2m-v2 control port, drives the exact
  cc65-Chess engine through `tests/uci`, referees games with python-chess, and
  writes CSV, PGN, JSON summaries, and failure logs.
- `levelcost.py` answers "how slow is each Sargon level" in six minutes, which
  is the question that decides whether a match at some level is an afternoon or
  a fortnight. It shares the boot choreography, the control port and the
  one-harness-at-a-time rule with `match.py`.
- `analyse.py` runs Stockfish over a finished PGN and attributes every large
  evaluation swing to the side that played it.
- `piececost.py` answers "what does moving each kind of piece cost, per move,
  by phase". Drop mate scores and print a median beside the mean — a plain mean
  over evaluation deltas is not a robust statistic.
- This file is the runbook. Update it whenever a new Sargon display or
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
of this directory. Supply your own copy at:

```text
sargon/Sargon-trimmed.dsk
```

It must be exactly **143,360 bytes** — a standard DOS 3.3 image — with SHA-256:

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

Recorded as provenance, not as a recommendation or a guarantee — check the hash
above rather than trusting any particular download, since what is served under
one name changes over time.

Two things that image is not. The commonly circulated `Sargon.dsk` is 153,394
bytes: the same 143,360-byte image followed by a 10,034-byte trailer, and a2m-v2
will not boot the oversized raw file — trimming it to the first 143,360 bytes
produces the hash above. And do **not** substitute the protected
`Sargon II.nib`; that image was observed announcing a false mate from the
initial position.

`match.py` validates the size, copies the file to
`/private/tmp/cc65-sargon-match.dsk`, and mounts that disposable working copy,
so nothing in this workflow can write to your original.

Python needs the `chess` package. Build the native UCI adapter before a run:

```sh
make -C tests -B uci
```

The repository test suite can be rebuilt and run with:

```sh
make -C tests test
```

## Exact Sargon boot choreography

The compilation disk is not directly bootable into the game. The reliable
sequence is:

1. Launch a2m-v2 with the trimmed disk and a control port. Do not use
   `--headless`.
2. Wait for the `SARGON ][` catalog/menu screen.
3. Send uppercase `R`, then uppercase `H`, as individual keys. Do not send
   Return with either key.
4. Ignore the transient `INSERT COPY DISK & HIT RETURN` screen. It clears by
   itself. Pressing Return there drops into the monitor.
5. Wait for the `NEW GAME` prompt.
6. Send `G` followed by Return.
7. At `YOUR COLOR`, send `W` or `B` followed by Return.
8. At `LEVEL OF PLAY`, send `0` through `6` followed by Return.

`R` and `H` must be uppercase. The distinction between the no-Return loader
keys and the later choice-plus-Return prompts is important.

After boot, send the control command `set-turbo max`. a2m-v2 does run at max,
but its window title currently does not update when speed is changed through
the control port.

## Reset behavior

The a2m-v2 `reset` control command returns this Sargon disk to the reusable
`NEW GAME` state. This was verified both textually and graphically:

- initial board frame SHA-256:
  `c8a566b8c37bddd5bdb5d23add6aefa2113cecc86753d58f86ae8d8c1a688f20`
- after `E2-E4 C7-C5`:
  `2275ee882c95532bcab053fbb194490ef8e9dd13a286cde2fe9d09a87e0e1d04`
- after reset and starting another game, the frame returned byte-for-byte to
  the initial hash.

Apple II snapshots are exposed by a2m-v2 but currently time out and are not
usable. Reset is therefore the per-game restoration mechanism.

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
game — four entries as White, and five entries of two replies each as Black.

`tests/uci` has `OwnBook` and `BookSeed`. `UCIEngine` sets both, and the
harness asks the engine what it plays. `--no-own-book` turns the tables off,
which is the control for anything measured with them on.

## Sargon opening randomization

A scripted reset otherwise repeats Sargon's opening choice. Sargon's keyboard
wait loop increments zero-page `$4E`, which supplies its opening entropy. At
the level prompt the harness:

1. pauses the emulator;
2. writes a deterministic byte to `$004E`;
3. queues the level and Return while paused;
4. resumes execution.

Color-swapped game pairs receive the same seed, and consecutive pairs advance
the low bits because Sargon's picker consumes only a few of them. The exact
seed is recorded implicitly by the batch name, game index, and retry number in
`seed_for()`.

**Sargon is not reproducible.** The `$4E` seed is written before the level is
typed, but the keyboard-wait counter keeps advancing afterwards, so book
choices depend on host timing. The same seed has opened `1.d4 Nf6` in one run
and `1.d4 d5` in the next with cc65's own moves identical. Paired replay of a
game index against an earlier run is therefore impossible; all comparisons
are unpaired.

cc65's own opening variety is small and bounded: four first moves as White,
and two replies to each of five White first moves as Black. Do not treat a
handful of repeated cc65 openers as a bridge failure — the rest of the
diversity comes from Sargon's book, Sargon's colour, and later play.
Summaries count distinct six-ply prefixes; that count often matters more than
the score.

## Text screen layout and move parsing

The Apple II text page is read from `$0400-$07ff` and decoded using the normal
24-row interleave. Sargon's move table has fixed columns:

- move number in the first ten columns;
- Player/White cell at columns 10-17;
- Sargon/Black cell at columns 23-30.

Which column belongs to Sargon depends on the selected color. Legal move
forms observed so far are:

```text
E2-E4       ordinary move
F5XD3       capture
0-0         king-side castle
0-0-0       queen-side castle
PXPEP       en passant
```

Coordinates are translated against python-chess's current legal move list,
which also resolves promotion to a queen. `CHECK` is a nonterminal annotation
after a coordinate move. `CHECKMATE` and `STALEMATE` are terminal displays;
on those screens Sargon may not put the normal player cursor in its usual
column.

### `MATE IN n` is status, not a move

Sargon can display `MATE IN 1` (and similar forecasts) for a long time while
searching. It can also put the forecast in the next move row after its real
coordinate move, for example:

```text
52  ...  C1-D2
53  `    MATE IN 1
```

The forecast must never be fed to the referee as a move. When the input
cursor has returned, the bridge compares the new table with the pre-move
snapshot and selects the newest newly-added valid coordinate token, ignoring
later status cells. Do not press a key merely because `MATE IN n` persists;
some Level 1 searches have taken more than a minute at emulator max.

### Move 100 is printed as `:0`

Sargon has only a two-character move-number field and does not implement a
hundreds digit. It advances the tens character through ASCII instead:

```text
99, :0 (100), :1, ... ;0 (110), ... ?0 (150)
```

The parser decodes `0` through `?` as digit values 0 through 15. `?0` is full
move 150, matching the harness's 300-ply safety cap. If this rollover is not
recognized, the bridge cannot see the player cursor at move 100 and appears to
hang.

## Referee and game policy

python-chess is an independent referee. It validates every coordinate move,
maintains the authoritative position, and determines game outcomes. Draw
claims are enabled, including threefold repetition and the fifty-move rule.
Games also have a 300-ply safety cap. Colors alternate every game.

Every completed game is appended immediately, so Ctrl-C is safe. Rerunning
the same command resumes at the next CSV index. A failed game is retried up to
three times; the retry changes its seed so one broken position cannot wedge an
entire batch. Failures are retained rather than silently discarded.

Each batch writes:

```text
<batch>.csv
<batch>.pgn
<batch>-summary.json
<batch>-failures.log     # only when a failure occurred
```

The CSV contains the full UCI move list as well as result, cc65 color, draw or
mate reason, ply count, duration, and six-ply opening prefix. The PGN is the
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

The emulator window remains visible. Sargon defaults to L1 for calibration.
To run a measured match explicitly:

```sh
python3 sargon/match.py \
  --only match \
  --match-games 64 \
  --cc-skill 3 \
  --sargon-level 4 \
  --output scratch/sargon-l4-YYYYMMDD
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
The calibration phase is a fixed screen — level 1, raise cc65 — and it
hard-codes the level and the batch name to match. Any other level is
`--only match --sargon-level N --match-games N`, which is also what names
the output `match-ccX-sargonY` instead of `calibration-...`.

A colour-split screen is two runs, and they want **separate output
directories**: `--cc-color` does not reach the batch name, so both halves would
otherwise resume into one CSV and the second half would start at the first
half's game index and inherit its seeds.

```sh
python3 sargon/match.py --only match --match-games 32 \
  --cc-skill 3 --sargon-level 4 --cc-color white --move-timeout 300 \
  --output scratch/sargon-l4-cc3-white-YYYYMMDD
```

`--cc-color` pins the colour for a whole run. The per-game seed advances every
game rather than every pair, because there is no colour-swapped partner to
hold it constant for.

`--no-own-book` turns the tables off.

And the cost of any of this before starting it:

```sh
python3 sargon/levelcost.py --output scratch/sargon-levelcost-YYYYMMDD
```

Do not run more than one harness on the default control port 6511. A second
read-only control client may also block because the active harness polls the
same emulator continuously.

## How long a run takes

`levelcost.py` plays the opening of one throwaway game per level and times
Sargon's replies; all seven levels take six minutes of emulator time.

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

`*` measured over whole games. The opening probe is representative through
level 4 (slightly pessimistic, if anything) and only half the story at
level 6, where a whole game costs about 1.9× its opening per move. Use the
probe as-is up to level 4, expect up to double it at 5 and 6, and check the
first few games of any long run against the estimate.

The wall between an afternoon and a fortnight sits between level 4 and
level 6. Sargon's levels are not a ladder that can be interpolated, and cost
per level is not strength per level.

Size `MOVE_TIMEOUT` at about 20× the projected per-move cost: the 120s default
is ample through level 3, 300 at level 4, 600 at level 5, and 1200 was used
for level 6. A search that overruns the timeout is scored as a bridge failure
and the game is retried.

At the cheap levels this harness is a large part of its own timing:
`player_move` types six keys at `KEY_DELAY` (0.15s) before Sargon has thought
at all, plus about 25ms of screen poll. That is nearly a third of the level-1
figure and 0.2% of the level-6 one. The host matters too: a timing figure
taken under load is about 11% pessimistic. Both programs are node limited, so
the match result does not care.

## Reading the games afterwards

```sh
python3 sargon/analyse.py scratch/<run>/smoke.pgn --depth 18 --engine cc65
```

Three cautions:

- **Stockfish judges by a standard neither program can play to.** A position
  it calls equal can be lost for a 60,000-node engine.
- **Mate scores clamp at ±10000**, so walking into a forced mate and failing
  to deliver one both print as ~9000cp swings. They are opposite errors.
- **A mean over evaluation deltas is not a robust statistic.** One clamped
  move outweighs two hundred ordinary ones. Drop the mate scores and print a
  median beside the mean, which is what `piececost.py` does.
- **A per-game count is not a per-move cost.** Counting queen moves per game
  correlates with the result because a losing position invites queen moves.

## Known a2m-v2 limitations and cautions

- Do not use `--headless`; the visible window is part of this study.
- `set-turbo max` works, but the window title does not reflect the control-port
  speed change.
- Apple II snapshots currently time out. Use verified reset instead.
- Do not send Return during the `R`, `H` boot-loader sequence.
- Do not type into the emulator window while automation owns focus; host
  keystrokes can be consumed by the Apple II.
- Do not assume `MATE IN n` means the program is finished.
- Do not assume decimal-only move numbers after move 99.
