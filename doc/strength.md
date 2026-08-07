# How strong is it, actually?

A companion to `engine.md`. That document explains how the engine works; this one asks a
harder question — *how well does it play?* — and takes the trouble to answer it properly.

The short version is at the top. Everything after it is the method, the results, and an
honest account of what the numbers do and do not mean, because a strength claim without its
caveats is just a boast.

---

## Executive summary

The engine was played against Stockfish 18 — one of the strongest chess programs in
existence — across roughly **20,000 games**. Each of the four skill levels from the game's
own menu was measured, and the results were cross-checked with two independent match
runners.

**The four levels, placed on Stockfish's own rating scale:**

| Menu level | Node budget | Time on a stock C64 | Approximate rating |
|---|---|---|---|
| 1 — Very Easy | 400 | 8 seconds a move | **~1170** |
| 2 — Easy | 1,200 | 29 seconds | **~1350** |
| 3 — Harder | 15,000 | ~3.5 minutes | **~1610** |
| 4 — Very Hard | 60,000 | ~15 minutes | **~1700** |

Each figure carries an honest uncertainty of about **±150 points** — not because the games
were few, but for reasons explained in §6 that no number of games would fix.

**Four findings worth pulling out of the tables:**

**The levels are real.** Each rung beats the one below it by 125 to 180 points, with no
overlap in the confidence intervals. The difficulty menu is not decorative.

**At its strongest setting the engine draws level with Stockfish's floor.** Stockfish
restricted to a single search node scored 179–159–174 against level 4 over 512 games — a
difference of +14 points, statistically indistinguishable from equality. A program written
for a 1 MHz 8-bit machine holds its own against the minimum configuration of a modern engine.

**Thinking twice as long is worth about 60 rating points.** Measured across the full range
from 400 to 60,000 nodes. This is the number that says what an accelerated emulator buys you,
and it is why level 4 is only 90 points above level 3 despite thinking four times as long.

**The engine throws away roughly one game in six that it has already won.** Not a rating
finding but a defect one, and the most valuable thing the exercise turned up. Details in §5.

---

# Part I — What is being measured, and why it is hard

## 1.1 Rating is a relative quantity

Chess ratings measure *differences*, not qualities. There is no instrument that reads a
program's strength off it directly. The only measurement available is: play a lot of games
against something, and observe the fraction of points scored.

That fraction converts to a rating difference by a fixed formula:

```
difference = -400 x log10(1 / score - 1)
```

Score 50% and the difference is zero. Score 75% and you are 191 points ahead. Score 25% and
you are 191 behind.

This has a consequence that shapes the whole exercise: **a rating is only ever a number
relative to some opponent, on some scale, in some pool of players.** "This engine is 1700"
is meaningless on its own. "This engine scores 50% against a configuration that Stockfish
labels 1700" is a claim you can check.

## 1.2 Why not just binary search for a 50% opponent?

The obvious plan is to hunt for the opponent setting where the engine wins half its games,
and read the rating off there. It works, but it wastes most of the games it plays.

The formula above turns *any* non-lopsided score into a rating difference. Anything between
roughly 15% and 85% is informative. So instead of bisecting toward 50%, it is better to play
a single ladder of opponent strengths in one sweep: every rung is a data point, the shape of
the curve is visible, and nothing is discarded as a failed probe.

## 1.3 How many games is enough?

At a score near 50%, the standard error on the rating difference is roughly `250 / sqrt(N)`
points. That gives:

| Games | 95% confidence |
|---|---|
| 16 | ±175 |
| 100 | ±70 |
| 512 | ±30 |
| 2,000 | ±15 |

This project had already learned the lesson the expensive way. During evaluation tuning, a
king-safety term looked like a mild improvement over 16 games and turned out, over 512, to
be a genuine 2.6-sigma *loss*. Small matches do not merely fail to answer — they answer
confidently and wrongly.

Every rung below is 512 games. They cost seconds.

---

# Part II — Method

## 2.1 Teaching the engine to talk

Chess programs converse in a protocol called UCI: the referee sends `position startpos moves
e2e4 e7e5`, the engine replies `bestmove g1f3`. Any program that speaks it can be played by
any tournament runner or examined by any analysis tool.

This engine did not speak it, because it had never needed to — it draws its own board on a
Commodore 64. Adding a translator was the single piece of work that made everything else in
this document possible, and it is small: a few hundred lines that convert between the
protocol's square names and the engine's internal ones, look each incoming move up in the
legal move list rather than reconstructing it, and hand back whatever the search chose.

Looking moves up rather than building them matters more than it sounds. Castling arrives
over the wire as an ordinary king move from e1 to g1, and en passant as an ordinary diagonal
pawn move. Only the engine's own generator knows those are special. Building the move by
hand would have produced something that looked right and quietly corrupted the position.

The translator is compiled for the desktop only — the 8-bit builds never see it, and gain
no bytes from its existence.

**What was verified before any game was played:** castling on both wings, en passant,
promotion including under-promotion to a knight, checkmate, stalemate, and the fifty-move
counter surviving a round trip through the protocol.

## 2.2 Fixed thinking, not a clock

Almost every engine measures its thinking in seconds. This one measures it in *nodes* —
positions examined — because it targets eight different 8-bit machines that share no common
timer, and because a node budget makes every one of them play the identical game.

That design decision turns out to be a considerable gift to the measurement. If both sides
are limited by node count rather than by a clock, then:

- the result does not depend on how fast the desktop running the match is;
- it does not depend on system load, or on how many games ran in parallel;
- it is **exactly reproducible**, today and in five years.

So every rung in the main ladder gives both engines a fixed node budget. The one exception
is discussed in §4.3, and it is flagged there precisely because it gives that property up.

## 2.3 The determinism problem, and the opening book

The engine is completely deterministic. Given a position it plays one move, always. Play it
against itself from the starting position a thousand times and you get the same game a
thousand times.

This is excellent for testing and fatal for measurement: a thousand identical games contain
exactly as much information as one.

The fix is to start each game from a different position. A set of **256 openings** was
generated by playing a few random legal moves from the start, discarding any that involve a
capture so that neither side begins with an advantage. Each opening is played **twice, with
the colours swapped**, so any bias in the opening itself cancels out. The set is generated
from fixed seeds rather than typed out by hand — a page of hand-written positions is a page
of transcription errors waiting to happen.

## 2.4 Validating the instrument before trusting it

**The single most important step, and the one most often skipped.** Before measuring
anything against Stockfish, the engine was played against *itself* — same version, same
settings, 512 games.

It must come out exactly level. If it does not, the harness is measuring something other
than chess strength — a colour bias, a bug in the translator, a fault in the opening set.

It came out **97 wins, 97 losses, 318 draws.**

Better still, the runner reports results grouped by opening pair, and all 256 pairs scored
exactly 1–1 — not one exception. That is a far sharper statement than the aggregate: it says
each opening produced mirror-image games, which is precisely what a deterministic engine
playing itself with reversed colours should do.

## 2.5 Two runners, in case one of them is wrong

The full ladder was run twice, using two independent tournament programs — **c-chess-cli**
and **fastchess** — which share no code and compute their rating estimates by different
routes. Every rung agreed within a few points.

This caught nothing, which is the point of doing it. It did, however, expose a trap worth
recording, because it is the kind of mistake that produces a table nobody questions.

**The near-miss.** fastchess reports two figures on the same line:

```
Elo: -428.75 +/- 45.31, nElo: -687.83 +/- 30.09
```

The second is *normalized* Elo, a different scale used for deciding when a test has run long
enough. The first automated read of that output picked up the wrong one. The resulting table
had the right signs, the right ordering, and a clean monotonic shape — it looked entirely
plausible and every number in it was on the wrong scale. It was caught only because the
figures disagreed with the hand calculation from the raw scores.

The general lesson: **a result that merely looks reasonable has not been checked.** Two
independent paths to the same number is how you check one.

---

# Part III — Choosing an opponent

Stockfish is close to two thousand rating points stronger than this engine. Measuring against
it at full strength would produce a score of zero and no information whatsoever. It has to be
weakened, and *how* turns out to be the most delicate decision in the whole exercise.

## 3.1 The floor nobody documents

Stockfish can be told to search a fixed number of nodes. The obvious approach is to dial
that number down until the two engines are comparable.

**It does not go as low as you would expect.** Stockfish restricted to 1 node, 3 nodes, 10
nodes and 30 nodes produced *identical* results — the same games, move for move, across
hundreds of them. It completes a minimum amount of work regardless of the budget it is
given, so all four settings are the same opponent wearing four different labels.

The usable dial starts around 50 nodes. Below that, a bisection search would have spent its
whole run distinguishing between things that are not different — and would have reported
convergence, since the numbers really are stable.

This is not a criticism of Stockfish; it is a reminder that a knob is only a knob over the
range where turning it does something, and that range is a thing you have to establish
rather than assume.

## 3.2 The gap between a ladder and a number

Fixed-node Stockfish makes an ideal *ladder*: reproducible, finely adjustable, and free of
any timing noise. What it cannot do is provide a *rating*, because "Stockfish at 100 nodes"
is not a configuration anyone has ever rated.

To convert the ladder into a number, one rung has to be tied to something with a published
figure. Stockfish has a strength-limiting mode calibrated in rating points, whose floor is
1320. That mode assumes the engine is playing against a clock, so this rung — and only this
rung — was played at a time control, and it gives up reproducibility to do it.

The result is that the ladder is the *measurement* and the anchor is the *label*.

---

# Part IV — Results

## 4.1 The ladder

512 games a rung, both sides limited by node count, 256 openings played twice with the
colours swapped. Figures are rating differences relative to the Stockfish setting named.

| Menu level | vs SF @ 1 node | @ 30 | @ 100 | @ 300 |
|---|---|---|---|---|
| 1 — Very Easy | −429 | −417 | −556 | −689 |
| 2 — Easy | −292 | −298 | −465 | −575 |
| 3 — Harder | −111 | −108 | −195 | −348 |
| 4 — Very Hard | **+14** | −6 | −53 | −256 |

Confidence intervals run from ±25 points at the top of the table to ±45 at the bottom. The
first two columns are identical within noise, for the reason given in §3.1.

**The ladder is clean.** Reading down the first column, the four levels are separated by
137, 181 and 125 points, in order, with no overlap between adjacent intervals. The four-item
difficulty menu delivers four genuinely different opponents — which was an assumption before
this exercise and is now a measurement.

**The headline row is level 4 against Stockfish at one node: 179–159–174 over 512 games,
+14 points, interval −12 to +39.** That is a dead heat. An engine that fits in 31 KB and runs
on a 1 MHz processor plays level with the weakest configuration a modern engine can be
persuaded into.

The honest framing of that result: Stockfish at one node is not playing chess in any
meaningful sense — it is making a well-informed snap judgement using a world-class evaluation
function. It is a low bar for Stockfish. It is still a real opponent, and matching it is a
real result.

## 4.2 What a doubling of thinking time buys

Because the engine's strength is a node budget, and the budgets are known exactly, the
ladder can be re-read as a curve of strength against effort.

From 400 nodes to 60,000 nodes is a factor of 150, or 7.2 doublings, across a total gain of
443 rating points. That is **about 61 points per doubling of thinking time.**

This is a useful number to carry around:

- Emulator acceleration is a free multiplier on thinking time. A 10x-accelerated machine is
  worth roughly 200 points at the same skill setting.
- Conversely, an optimisation that makes the search twice as fast is worth about 60 points —
  real, but not transformative. The two changes that doubled the search speed on real
  hardware are worth slightly under half of one step on the difficulty menu.
- Diminishing returns are visible in the table: the step from level 3 to level 4 quadruples
  the budget for only 125 points.
- And it sets the price of a rating point. Another 100 points from thinking alone would need
  roughly triple the node budget — about 45 minutes a move on a stock C64, and around 187,000
  nodes. That number does not fit: budgets are held in a 16-bit counter, which stops at
  65,535, and level 4 already spends 60,000 of it. **The engine is within 10% of the
  strongest setting it can currently express.** Past that, strength has to come from playing
  better per node, not from searching more of them.

## 4.3 The anchor, and why it is the soft part

Against Stockfish's rating-limited mode at its 1320 floor, at a two-second time control:

| Menu level | Run A (256 games) | Run B (128 games) | Implied rating |
|---|---|---|---|
| 1 | −126 | −180 | ~1140–1195 |
| 2 | +27 | +27 | ~1347 |
| 3 | +264 | +314 | ~1584–1634 |
| 4 | +364 | +402 | ~1684–1722 |

**The two runs disagree by 40 to 50 points**, which is larger than either one's statistical
interval. That is not sloppiness — it is the honest scale of the uncertainty, and it comes
from three sources that more games would not reduce:

1. Stockfish at a time control is not deterministic, so the opponent itself varies.
2. Its rating calibration assumes a normal game length, not two seconds.
3. Rating differences are not transitive. Measuring A against B and B against C does not
   reliably give A against C, especially when the styles differ.

There is a fourth, visible in the data: the ladder and the anchor disagree about the *spread*
of the four levels. The ladder puts level 1 and level 4 exactly 443 points apart. The anchor
puts them 490 apart on one run and 582 on the other. Both cannot be right, and the
disagreement is around 20%.

**This is why the executive summary says ±150.** The statistical error is ±30. The
methodological error is five times larger, and stating only the first would be dishonest.

## 4.4 The correct form of the claim

> *Level 4 scores 50% against Stockfish restricted to a single search node, and roughly 89%
> against Stockfish's rating-limited mode set to 1320. That places it near 1700 on
> Stockfish's own scale, give or take 150.*

Note what that sentence does **not** say. It does not say the engine is a 1700-rated player.
Engine rating lists, Lichess ratings and FIDE ratings are three different pools with three
different scales, and a number from one does not transfer to another. A club player rated
1700 by their national federation would find this engine unfamiliar rather than equal — it
never gets tired, never miscalculates a two-move tactic, and has no idea what a plan is.

---

# Part V — What the games revealed

The rating number was the stated goal. The games themselves turned out to be worth more.

## 5.1 One game in six, thrown away

In the 512-game self-play match, 318 games — **62%** — were drawn. Every single one of them
was a draw by threefold repetition. Not one fifty-move draw, not one stalemate.

The engine has no repetition detection. Nothing in the search knows that returning to a
position it has already visited is worthless, so in a position where it cannot find progress
it will happily shuffle a piece back and forth forever.

That is a known gap, and it was previously assumed to be a minor one. It is not. Grouping
those 318 draws by the engine's *own* evaluation of the position at the moment it repeated:

| The engine's own verdict when it repeated | Games | Share |
|---|---|---|
| Under 0.5 pawns — genuinely balanced | 60 | 19% |
| 0.5 to 2 pawns — an edge | 78 | 25% |
| 2 to 5 pawns — winning | 88 | **28%** |
| Over 5 pawns — completely won | 92 | **29%** |

Median: 2.35 pawns. Worst case: 23 pawns — an entirely decided game.

**Fifty-seven percent of those draws happened in positions the engine itself scored as
winning.** Ninety-two games — 18% of the whole match — were drawn while a rook or more ahead.

Two things make this finding worth more than the rating it sits next to.

**Self-play could never have found it.** When both sides share the defect, a repetition looks
like a fair result. It takes an external opponent, or the engine's own evaluation read back
against the outcome, to see that a full point was on the table.

**It converts a to-do into a price.** "No repetition detection" had been on the list of known
absences for some time, alongside several other absences, with no way to rank them. It is now
the only one with a number attached, and the number is roughly a sixth of the engine's score.

## 5.2 The opening is dull, and the reason is mundane

From the starting position the engine plays a knight to c3. Every time. It is not a bad move;
it is simply always the same move, which makes the first few moves of every game identical.

The cause is not mysterious. The evaluation frequently rates several opening moves exactly
equal, and ties are broken by whichever move the generator produced first — which is a
property of the loop order, not of chess.

The fix is inexpensive: pick randomly among moves within a few hundredths of a pawn of the
best. The complication is that it would destroy the determinism that every measurement in
this document depends on, so it needs to be switchable rather than simply added — random for
a human opponent, deterministic for the harness.

## 5.3 The other thing that did not happen

No illegal move was played, and no game was lost on time, across roughly 20,000 games.

That is worth stating because it was not guaranteed. The move generator had been verified
by node-counting to a fixed depth from a handful of standard positions — a strong test, but a
*shallow* one, exploring a few million positions near the start of a game. A real game is 70
to 150 moves deep and wanders into endgames, promotion races and fortress positions that a
depth-5 count never reaches. Twenty thousand games is a very different kind of test, and the
generator passed it without a single complaint from either referee.

---

# Part VI — Threats to validity

Everything above is true and none of it is the whole truth. The honest list of what could
make these numbers wrong:

**The opening set is artificial.** Positions reached by four random non-capturing moves are
not positions humans reach. They are balanced and varied, which is what the measurement
needs, but they are slightly strange, and an engine's strength is not perfectly uniform
across position types.

**Ratings are pool-dependent.** Every figure here lives on Stockfish's scale. It is not FIDE,
not Lichess, and not any published engine rating list.

**The anchor rung is not reproducible** and its two runs disagree by 40–50 points (§4.3).

**Fixed-node Stockfish is not a rated configuration.** The ladder is internally consistent
and externally uncalibrated; it borrows its calibration entirely from the anchor.

**The engine is deterministic**, so all game-to-game variation comes from the opening set.
With 256 openings the effective sample is smaller than 512 independent games would be — which
is exactly why the pairing statistics in §2.4 are reported alongside the raw scores.

**Draw rates are high** in self-play (62%), which narrows the confidence intervals in a way
that is technically correct but flatters the precision.

---

# Part VII — Reproducing this

The whole measurement is a handful of commands and a few minutes of desktop time.

**What is needed:** a match runner (either fastchess or c-chess-cli — both are small and
build from source in about a minute), and Stockfish, which is a package on every platform.

**The sequence:**

1. Build the desktop version of the engine with the protocol translator attached.
2. Generate the 256-position opening set. It is reproducible from fixed seeds, so
   regenerating it gives a byte-identical file and results stay comparable across runs.
3. **Run the self-play validation first, and do not proceed if it is not level.** 512 games,
   a few seconds. This is the step that catches a broken harness before it produces a
   convincing wrong answer.
4. Run the ladder: four skill levels against four opponent settings, 512 games each.
5. Run the anchor separately, and treat it with the suspicion §4.3 earns it.

The whole ladder is roughly 8,000 games and takes a few minutes. **There is no reason ever to
run a short match.**

The build targets, script names and exact command lines live in `tests/`, which is where they
will stay current; naming them here would only date this document.

---

# Appendix A — The complete ladder

512 games a rung. Rating difference relative to the Stockfish setting, 95% interval, and the
opening-pair breakdown (both games lost, split, both drawn, split the other way, both won).

| Level | Opponent | W–L–D | Score | Diff | 95% interval |
|---|---|---|---|---|---|
| 1 | SF 1 node | 13–445–54 | 0.078 | −429 | [−474, −383] |
| 1 | SF 30 nodes | 14–441–57 | 0.083 | −417 | [−463, −372] |
| 1 | SF 100 nodes | 9–481–22 | 0.039 | −556 | [−623, −490] |
| 1 | SF 300 nodes | 4–497–11 | 0.019 | −689 | [−792, −587] |
| 2 | SF 1 node | 30–381–101 | 0.157 | −292 | [−324, −259] |
| 2 | SF 30 nodes | 30–386–96 | 0.152 | −298 | [−331, −265] |
| 2 | SF 100 nodes | 20–466–26 | 0.064 | −465 | [−522, −408] |
| 2 | SF 300 nodes | 10–486–16 | 0.035 | −575 | [−649, −502] |
| 3 | SF 1 node | 99–257–156 | 0.346 | −111 | [−136, −86] |
| 3 | SF 30 nodes | 107–261–144 | 0.350 | −108 | [−134, −81] |
| 3 | SF 100 nodes | 79–340–93 | 0.245 | −195 | [−226, −165] |
| 3 | SF 300 nodes | 39–429–44 | 0.119 | −348 | [−389, −306] |
| 4 | SF 1 node | 179–159–174 | 0.520 | **+14** | [−12, +39] |
| 4 | SF 30 nodes | 165–174–173 | 0.491 | −6 | [−31, +19] |
| 4 | SF 100 nodes | 159–237–116 | 0.424 | −53 | [−79, −27] |
| 4 | SF 300 nodes | 70–391–51 | 0.187 | −256 | [−292, −220] |

Cross-check against the second runner, first column, 512 games each: −429, −290, −105, +10.

# Appendix B — Self-play reference

512 games, level-equivalent settings, engine against itself.

| | |
|---|---|
| Result | 97–97–318 — exactly level |
| Opening pairs | 256 of 256 scored 1–1 |
| Draws | 318 (62%), **all** by threefold repetition |
| Median game | 72 plies |
| Longest game | 151 plies |

# Appendix C — On provenance

This engine was written with AI assistance, and so was this measurement. That is worth
stating plainly rather than leaving to be inferred, because a reader is entitled to know how
a thing was made when judging what it demonstrates.

It does not change what the numbers mean. Twenty thousand games were played by two
independent referees against a program nobody here wrote, and the harness was validated
against a result known in advance. Those checks exist precisely so that the answer does not
depend on trusting whoever — or whatever — assembled the question.
