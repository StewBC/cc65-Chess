# How strong is it, actually?

A companion to `engine.md`. That document explains how the engine works; this one asks a
harder question — *how well does it play?* — and takes the trouble to answer it properly.

The short version is at the top. Everything after it is the method, the results, and an
honest account of what the numbers do and do not mean, because a strength claim without its
caveats is just a boast.

---

## Executive summary

The engine was played against Stockfish 18 — one of the strongest chess programs in
existence — across roughly **40,000 games**. Each of the four skill levels from the game's
own menu was measured, and the results were cross-checked with two independent match
runners.

**The four levels, placed on Stockfish's own rating scale:**

| Menu level | Node budget | Time on a stock C64 | Approximate rating |
|---|---|---|---|
| 1 — Very Easy | 400 | 9 seconds a move | **~1240** |
| 2 — Easy | 1,200 | 33 seconds | **~1430** |
| 3 — Harder | 15,000 | ~4 minutes | **~1700** |
| 4 — Very Hard | 60,000 | ~17 minutes | **~1950** |

Each figure carries an honest uncertainty of about **±150 points** — not because the games
were few, but for reasons explained in §4.3 and §6 that no number of games would fix.

**Five findings worth pulling out of the tables:**

**The levels are real.** Each rung beats the one below it by 105 to 200 points, with no
overlap in the confidence intervals. The difficulty menu is not decorative.

**At its strongest setting the engine is well past Stockfish's floor.** Level 4 scored
298–82–132 against Stockfish restricted to a single search node over 512 games, a difference of
+156, and draws level with it at a hundred nodes. *Level 3* now holds the dead heat at one node
that level 4 held a version ago.

**Thinking twice as long is worth about 60 rating points.** Measured across the full range
from 400 to 60,000 nodes, and unchanged across three strength changes that lifted the whole
ladder without altering its shape. This is the number that says what an accelerated emulator
buys you, and it is why level 4 is only 136 points above level 3 despite thinking four times
as long.

**The engine threw away roughly one game in six that it had already won.** Not a rating
finding but a defect one, and the most valuable thing the exercise turned up. It has since
been fixed — details and the measurement in §5.1.

**Three instruments measured the endgame tables and gave three answers.** Self-play said +44,
the ladder said +52 to +94, and the rated anchor said approximately nothing. §5.1.3 works
through which to believe and why the honest claim is narrower than any single number.

**The largest defect in the project was found by a person, not by 40,000 measured games.**
Level 1 hung mate in one more than half the time, because quiescence stood pat and looked only
at captures even when the king was in check. §5.1.5 has the fix and what it was worth — between
+28 and +102 Elo a level at equal time, which is the largest single gain recorded here.

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
| 1 — Very Easy | −285 | −319 | −429 | −570 |
| 2 — Easy | −180 | −183 | −246 | −434 |
| 3 — Harder | +20 | +6 | −67 | −215 |
| 4 — Very Hard | **+156** | +136 | +19 | −115 |

Confidence intervals run from ±26 points at the top of the table to ±69 at the bottom. The
first two columns are identical within noise, for the reason given in §3.1.

**The ladder is clean.** Reading down the first column, the four levels are separated by
105, 200 and 136 points, in order, with no overlap between adjacent intervals. The four-item
difficulty menu delivers four genuinely different opponents — which was an assumption before
this exercise and is now a measurement. The uneven spacing is not a defect: the node budgets
step by 3x, 12.5x and 4x, and §4.2 shows those gaps are exactly what that buys.

**The headline row is level 4 against Stockfish at one node: 298–82–132 over 512 games,
+156 points.** An engine that fits in 31 KB and runs on a 1 MHz processor is well past the
weakest configuration a modern engine can be persuaded into. Equality now sits at Stockfish
**100 nodes**: +19, interval −7 to +45. Level 3 has arrived where level 4 used to be, drawing
level with Stockfish at one node.

The honest framing of that result: Stockfish at one node is not playing chess in any
meaningful sense — it is making a well-informed snap judgement using a world-class evaluation
function. It is a low bar for Stockfish. It is still a real opponent, and clearing it is a
real result.

**These numbers are current as of the endgame piece-square tables.** The ladder has been run
three times over the life of the project, and §5.1.3 sets the three generations side by side —
that comparison, not this table, is where the strength changes are visible.

## 4.2 What a doubling of thinking time buys

Because the engine's strength is a node budget, and the budgets are known exactly, the
ladder can be re-read as a curve of strength against effort.

From 400 nodes to 60,000 nodes is a factor of 150, or 7.2 doublings, across a total gain of
443 rating points. That is **about 61 points per doubling of thinking time.**

That figure has now survived three strength changes without moving. The first-column spread was
443 points before repetition detection and the endgame tables, 443 after them, and **441** after
check evasions — the whole ladder lifted three times and the shape of it never changed. The
individual steps are the same story: 3x, 12.5x and 4x in budget predict 97, 222 and 122 points
at 61 a doubling, and the ladder measures 105, 200 and 136. Whatever these changes did, they
did not change the price of a node.

This is a useful number to carry around:

- Emulator acceleration is a free multiplier on thinking time. A 10x-accelerated machine is
  worth roughly 200 points at the same skill setting.
- Conversely, an optimisation that makes the search twice as fast is worth about 60 points —
  real, but not transformative. The two changes that doubled the search speed on real
  hardware are worth slightly under half of one step on the difficulty menu.
- Diminishing returns are visible in the table: the step from level 3 to level 4 quadruples
  the budget for only 113 points.
- And it sets the price of a rating point. Another 100 points from thinking alone would need
  roughly triple the node budget — about 45 minutes a move on a stock C64, and around 187,000
  nodes. That number does not fit: budgets are held in a 16-bit counter, which stops at
  65,535, and level 4 already spends 60,000 of it. **The engine is within 10% of the
  strongest setting it can currently express.** Past that, strength has to come from playing
  better per node, not from searching more of them.

## 4.3 The anchor, and why it is the soft part

The ladder measures differences. Turning those into ratings needs an opponent whose rating is
claimed rather than inferred, and Stockfish's `UCI_LimitStrength` mode is the only one
available. It comes with a clock — its calibration assumes one — so this is the single
measurement in this document that is not reproducible.

Each level is played against the two rated rungs nearest its own strength, 256 games each, at
`tc=4+0.04`. Reading a rating off a rung the engine scores 88% against tells you very little,
which is a mistake this section used to make.

| Menu level | Rung | W–L–D | Score | Diff | Implied rating |
|---|---|---|---|---|---|
| 1 | SF Elo 1320 | 94–155–7 | 0.381 | −84 | **1236** |
| 1 | SF Elo 1500 | 41–210–5 | 0.170 | −276 | 1224 |
| 2 | SF Elo 1500 | 99–148–9 | 0.404 | −67 | **1433** |
| 2 | SF Elo 1700 | 49–198–9 | 0.209 | −231 | 1469 |
| 3 | SF Elo 1700 | 125–124–7 | 0.502 | **+1** | **1701** |
| 3 | SF Elo 1900 | 97–153–6 | 0.391 | −77 | 1823 |
| 4 | SF Elo 1900 | 141–107–8 | 0.566 | +46 | **1946** |
| 4 | SF Elo 2100 | 76–160–20 | 0.336 | −118 | 1982 |

**Level 3 against Stockfish rated 1700 is 125–124–7**, a score of 0.502 — as well-placed an
anchor point as this project has produced, needing no extrapolation at all. It is also exactly
where *level 4* stood before check evasions, which is the clearest single statement of what
that change was worth.

One game at the level 2 / 1700 rung was lost on time and the runner flagged it; the rung is
reported as measured rather than quietly dropped, and one game in 256 does not move it.

**These are not comparable rung-for-rung with the previous run.** Every level moved up, so
every level was measured against *stronger* rungs than last time — and §4.3.1 shows the implied
rating rises with the rung whatever the engine does. Level 4 reading 1946 against 1701 before
is therefore an overstatement of a real gain, not a measurement of it. The ladder, which is
rung-for-rung identical across runs, says the gain was +63 at equal nodes.

### 4.3.1 The anchor's answer depends on which rung you read it at

Every level reads *higher* the stronger the rung it is measured against, and the effect is
large. Measured twice, on two different engines, and it does not go away:

| Level | Rung step | Nominal gap | Measured gap | Ratio |
|---|---|---|---|---|
| 1 | 1320 → 1500 | 180 | **192** | 0.9 |
| 2 | 1500 → 1700 | 200 | 164 | 1.2 |
| 3 | 1700 → 1900 | 200 | 78 | 2.6 |
| 4 | 1900 → 2100 | 200 | **164** | 1.2 |

A 200-point step in `UCI_Elo` is generally not worth 200 points of played strength, and the
shortfall grows with the rung: Stockfish's rating limiter runs out of ways to be weak that a
four-second clock does not already impose. Level 3 is "1701" or "1823" depending purely on which
rung you ask, and the honest reading is the rung nearest a 50% score.

The previous run's version of this table, on the pre-check-evasion engine, read 1.4 / 1.2 /
1.6 / **3.6** — the 3.6 being level 4 across 1700 → 1900, where the two rungs played almost the
same chess. That cell is the reason the ±150 in the executive summary is a measurement rather
than a guess, and why a level's rating should never be quoted from a rung it scores 85% against.

**This is what ±150 means, and it is now measured rather than asserted.** The statistical
interval on any single rung above is about ±40. The disagreement between two rungs measuring
the same thing is 144.

### Two other things that would have quietly corrupted this

**Games lost on time.** The engine ignores every clock and stops at a node count, so it cannot
legitimately lose on time — but the harness has to hand fastchess *some* limit, and the 30 ms
it used to pass was close enough to a level 4 move (~15 ms here) that host load alone forfeited
games. The first anchor run flagged four. The limit is now 5000 ms, which cannot change a move
that gets played, and the node-limited ladder reproduces to the digit across the change —
level 4 against Stockfish at one node was 248–129–135 before and after.

**Concurrency.** A time-controlled match run with as many games in parallel as the host has
cores is partly a measurement of the host. These rungs run at concurrency 4 on 8 cores; the
node-limited ladder does not care and runs at 8.

Neither of these touches the ladder in §4.1. Both touch the anchor, which is one more reason
it is the soft part.

## 4.4 The correct form of the claim

> *Level 4 scores 71% against Stockfish restricted to a single search node, 53% against it at
> 100 nodes, and 57% against Stockfish's rating-limited mode set to 1900. That places it near
> 1950 on Stockfish's own scale, give or take 150.*

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

## 5.1.1 What fixing it was worth

The price above is what bought the work. Repetition detection is now in the engine —
§6.10 of `doc/engine.md` describes the mechanism — and this is what it measured.

Both sides of the match maintain the incremental hash, and only one of them scores a repeat as
a draw, so the first line isolates the value of the *detection*. The second charges it for
itself: the detecting side gets the node budget the hash's cost leaves it.

| | Result | Score | Elo | |
|---|---|---|---|---|
| Equal nodes, 2000 each | 155–91–266 | 0.5625 | **+44** [+23, +65] | 4.1 sigma |
| Equal time, 1832 v 2000 | 146–90–276 | 0.5547 | **+38** [+18, +59] | 3.7 sigma |

And in self-play — the same 512 games, the engine against itself, which is where the defect was
found:

| | Draws | Decisive | How they were drawn |
|---|---|---|---|
| Before | 272 (53%) | 240 | 74 fifty-move, 196 ran out the ply limit |
| After | 162 (32%) | 350 | 62 threefold, 14 fifty-move, 84 ply limit |

**The equal-time column is charged at the target's cost, not this host's.** Maintaining the
hash costs 5.5% measured here and 9% measured on a real C64, and the C64 is where the program
runs. Taking the host figure would have credited the change with about six points it has not
earned. This is the same lesson as §6.9 of `doc/engine.md` in a different disguise: a cost
measured on the wrong machine is not the cost.

**Three caveats, none of which the numbers above hide.** These are self-play results, and
self-play systematically overstates a change that both sides understand. The opening set is the
same artificial one described in §6. And 512 games with a 3.7-sigma edge is a real result, not
a certainty.

The first of those caveats can be checked rather than asserted, and §5.1.2 does.

## 5.1.2 The same change, measured against somebody else

The ladder was re-run after repetition detection landed: same 512 games a rung, same Stockfish
18, same book, both sides node limited — the conditions of Appendix A exactly. The differences
against the recorded table:

| Level | vs SF 1 node | vs SF 30 | vs SF 100 | vs SF 300 | mean |
|---|---|---|---|---|---|
| 1 | +7 | −21 | −30 | −20 | **−16** |
| 2 | −11 | −26 | 0 | −11 | **−12** |
| 3 | +20 | +20 | +17 | +5 | **+16** |
| 4 | +16 | +16 | +5 | 0 | **+9** |

**Self-play overstated the gain about threefold.** It said +38 at equal time; against an
opponent that does not share the defect it is +9 to +16 at the levels where it helps. That is
the caveat above turned into a number, and it is a large one. Any future self-play result in
this document should be read with it in mind.

**Levels 1 and 2 came out slightly worse, and the pattern says where.** Every one of the eight
deep-level rungs is at or above zero; seven of the eight shallow ones are at or below it. The
damage is concentrated against opponents far stronger than the engine, and against the weakest
opponent level 1 actually improved.

The mechanism is a hypothesis, not a finding: a repetition is often the *worse* side's best
result, and scoring it as a draw makes the engine decline one whenever its evaluation claims
better than zero. At 400 to 1200 nodes that evaluation is frequently wrong, so it walks out of
a draw it should have taken. Level 2 against Stockfish at 30 nodes bears the shape of it —
draws 96 → 65, losses 386 → 411, wins 30 → 36, draws converted into losses about four to one.

Per-rung intervals are ±25 to ±50, so no single cell above is significant; the sign pattern
across sixteen rungs is what carries it. Whether it matters for the *purpose* of those levels
is a separate question, since a human beginner is far closer to Stockfish at one node than at
three hundred, and no one has measured that.

**This hypothesis has since been tested directly — §5.1.4.** It half survives: the mechanism is
visible at level 1 and worth about +15 Elo, and at level 2 it is worth nothing.

## 5.1.3 The endgame tables, measured the same way

The endgame piece-square tables — §6.11 of `doc/engine.md` — were accepted on a self-play
result of **+44 Elo at equal nodes and +30 at equal time**, the first evaluation term in the
project to survive the equal-time test. They are the only change to the engine since the run
in §5.1.2; everything else that landed in between was a platform file, a test, or a document.
So the ladder can price them against an outside opponent exactly as it priced repetition
detection.

| Level | vs SF 1 node | vs SF 30 | vs SF 100 | vs SF 300 | mean |
|---|---|---|---|---|---|
| 1 | +61 | +75 | +127 | +112 | **+94** |
| 2 | +53 | +59 | +90 | +107 | **+77** |
| 3 | +60 | +59 | +50 | +89 | **+65** |
| 4 | +52 | +55 | +39 | +62 | **+52** |

**Sixteen rungs out of sixteen improved.** That is the part worth stating first, because no
individual cell is significant on its own — the intervals are ±26 to ±80 — and a unanimous
sign pattern across sixteen rungs is not something a null effect produces.

**Self-play understated this one against the ladder, having overstated the last one.** §5.1.2
found self-play inflating repetition detection about threefold. Against the ladder it did the
opposite here: it claimed +44 at equal nodes and the ladder measures +52 to +94, between one
and two times as much again. The useful conclusion is not that self-play is pessimistic — it is
that **the bias has no fixed sign, so it cannot be corrected with a factor.** Two changes, two
directions. A self-play number is a reason to go and measure against somebody else, and nothing
more than that. The rated anchor, asked the same question below, gives a third answer again.

There is a mechanism for the direction in this case, and it is the same one that made these
tables worth having. Self-play puts endgame knowledge on both sides of the board, where much
of what it buys is spent against an opponent who knows the same thing and the game is drawn
anyway. The ladder puts it against an opponent that has *better* endgame knowledge already,
so what the tables buy is the engine failing to lose won and drawn endings — which shows up
as points.

**The gain is monotone in how little the engine searches:** +94, +77, +65, +52 down the
levels. Knowledge substitutes for search, and it substitutes hardest where there is least
search to substitute for. Level 1 sees 400 nodes and gained nearly twice what level 4 gained
at 60,000. This is the first change in the project where the shallow levels were the biggest
winners, and it is the reason the concern in §5.1.2 is now closed — see below.

**What this table does not charge for.** Every rung is a fixed node budget on both sides, so
the tables' 9% cost per node on a real C64 is not paid anywhere in it. That is not a cheat, it
is what a skill level *is* here: level 4 means 60,000 nodes, and 60,000 nodes now play about
50 points better and take about 9% longer on the board. The equal-time question was asked and
answered in self-play (§5.1.1, +30), which is the number to quote if the 9% has to be charged
somewhere.

### The anchor does not see it, and that is the interesting part

The ladder is not the only instrument pointed at this change. The rated anchor of §4.3 was run
before the endgame tables and after, and where the two overlap — Stockfish's 1320 rung, the
only configuration measured on both sides of the change — it says something else:

| Level | Ladder, mean of four rungs | Anchor at SF 1320 |
|---|---|---|
| 1 | +78 | +39 |
| 2 | +65 | −3 |
| 3 | +80 | +7 |
| 4 | +61 | −37 |

Both anchor columns average their runs; the level 3 and 4 figures come from the first re-run,
before the movetime fix, since the clean run played those levels at rungs the old engine was
never measured against. The four forfeited games were all at level 2.

Two explanations, and they are not exclusive.

**The 1320 rung cannot resolve it.** Levels 3 and 4 score 0.86 and 0.88 there, past the top of
the informative band §1.2 describes. At that score a couple of games move the Elo figure tens
of points, which is visible in the old run's own replicates: level 4 measured +364 and +402 on
two runs of the same unchanged engine, a spread of 38. The level 3 and level 4 rows above are
differences between numbers that were never that precise. Levels 1 and 2 sit in the middle of
the band and are the ones worth reading, and they disagree too.

**The gain may be partly specific to the opponent that showed it.** This is §4.3's third
source of error — non-transitivity — arriving with evidence. Stockfish at 1 to 300 nodes is
not searching; whatever it plays in an endgame is a raw evaluation of the position in front of
it. Stockfish with four seconds on a clock plays endgames properly. A change that is *entirely
endgame knowledge* is worth more against the first opponent than against the second, and the
ladder is built from the first.

**Neither instrument is being ignored here.** Self-play said +44, the ladder says +52 to +94,
the anchor says approximately nothing, and all three were measured carefully. The claim the
evidence supports is the narrow one: **the endgame tables are worth a great deal against
opponents that do not search endgames, and are not yet shown to be worth much against
opponents that do.** They also raised endgame conversion from 87% to 90% and openings from 69%
to 78% in self-play (§5.1.1), which is a defect measurement rather than a rating one and is not
in dispute.

The way to settle it is a rated-anchor rung near 50% measured on both sides of the change,
which cannot be done retroactively — the pre-change engine's anchor was only ever run at 1320.
It is the sort of thing worth setting up before the next evaluation term, not after.

## 5.1.4 The levels 1 and 2 hypothesis, tested

§5.1.2 recorded levels 1 and 2 coming out −16 and −12 after repetition detection, offered a
mechanism, and admitted it was a hypothesis with no test behind it: *a repetition is often the
worse side's best result, and scoring it as a draw makes the engine decline one whenever its
evaluation claims better than zero.*

Two things have since made it testable. The first is arithmetic — those levels have gained
+94 and +77 from the endgame tables, so against the original ladder they now stand **+78 and
+65**, the two largest improvements of the four, which closes it as a *problem*. The second is
that the mechanism itself can now be switched: `make uci-tuning` builds the UCI adapter with
`-DEVAL_TUNING`, and `--uci-option Repetition=false` turns the detection off for one side of a
ladder run. With everything on it reproduces the shipped binary's games to the digit on all six
rungs, which is what makes the difference attributable.

512 games a rung, repetition detection off minus on, so a positive number means the engine did
*better* without it:

| Level | vs SF 1 node | vs SF 100 | vs SF 300 | mean |
|---|---|---|---|---|
| 1 | +37 | +3 | +6 | **+15** |
| 2 | −1 | −6 | +9 | **+1** |

**The hypothesis survives in direction at level 1 and dies at level 2.** Every rung's draw
count rises with detection off — level 1 goes 76 → 97, 46 → 51, 14 → 21 — which is the
mechanism doing exactly what §5.1.2 said it would: without detection the engine shuffles into
repetitions, and against an opponent it cannot beat, a repetition is a point saved. The losses
fall to match: 417 → 395 at the first rung.

**But at level 2 the same extra draws come out of the wins.** Draws 110 → 129 while wins go
43 → 33 and losses only 359 → 350. Level 2 searches three times as deep and some of those
repetitions were positions it could have won, so declining them is correct there and the net
is zero.

**And the one large cell is not significant.** +37 at level 1 against Stockfish at a single
node carries an interval of roughly ±52 on the difference of two independent matches. Three
rungs averaging +15 with a consistent sign is suggestive; it is not a finding, and it is a
sixth of what the endgame tables gave the same level.

**Nothing here is a case for removing repetition detection.** It exists because the engine was
throwing away one game in six that it had already won (§5.1), which is a defect worth more than
15 Elo at one skill level, and the tuning switch is a measuring instrument rather than a
proposal. What the test does retire is the open question: the shallow levels do not pay a
meaningful price for it, and §5.1.2's concern can stop being carried forward.

## 5.1.5 The largest defect in the project, found by a human at a board

**Level 1 hung mate in one more than half the time.** Not a subtle positional weakness — the
weakest two levels simply could not see a checkmate they could play that move. It was reported
by somebody playing the game on an Apple II, which is worth stating plainly: 40,000 measured
games had not found it, and one person looking at a screen did.

The measurement, once it was looked for, took a minute. Sixty mate-in-one positions taken from
random games, every one verified by playing the move and confirming the opponent is in check
with no legal reply:

| | before | after |
|---|---|---|
| level 1 — 400 nodes | **27 / 60** | 55 / 60 |
| level 2 — 1,200 nodes | 54 / 60 | 58 / 60 |
| level 3 | 60 / 60 | 60 / 60 |
| level 4 | 60 / 60 | 60 / 60 |

**The cause is one line, and the shape of it is general.** `negamax` at depth 0 handed straight
over to quiescence, which stands pat on a static evaluation and looks only at captures. Neither
behaviour is legal when the side to move is in check: there is no declining to move, and the
move that escapes may be quiet. So a mating move looked like an ordinary quiet move, and mate
in one was invisible to a depth 1 search. Level 1's 400 nodes rarely reach depth 2 — the note in
`gcSearchSkill` measures depth 2 at ~1159 nodes in a middlegame — so the failure concentrated
exactly there. Sorting level 1's results by the deepest iteration it completed makes it
unambiguous:

```
completed depth 1:   4 solved, 32 missed
completed depth 2:  23 solved,  0 missed
```

The fix is that quiescence generates evasions rather than captures when in check, does not
stand pat, and returns a mate score when there is nothing legal. §6.10b of `doc/engine.md` has
the code.

**What it was worth, at equal nodes** — every rung of the ladder, against the table it replaced:

| Level | vs SF 1 node | vs SF 30 | vs SF 100 | vs SF 300 | mean |
|---|---|---|---|---|---|
| 1 | +76 | +44 | +30 | +27 | **+44** |
| 2 | +70 | +82 | +129 | +45 | **+82** |
| 3 | +51 | +35 | +61 | +39 | **+47** |
| 4 | +74 | +71 | +28 | +79 | **+63** |

**And at equal time, which is the number that counts.** A node costs 12.2% more with evasions
on, measured over identical work rather than inferred from two matches — an important
distinction, because the first estimate came from comparing two matches that had played
different games and was wrong by more than double. That figure is a *host* figure and the
target is not the host: §5.1.1 found the position hash costing 5.5% here and 9% on a real C64,
so the honest thing is to charge more than measured. Charged **20%** — the fixed engine given
proportionally fewer nodes, its opponent unchanged:

| Level | budget, on vs off | Elo at equal time |
|---|---|---|
| 1 | 333 v 400 | **+28** |
| 2 | 1,000 v 1,200 | **+102** |
| 3 | 12,500 v 15,000 | **+51** |
| 4 | 50,000 v 60,000 | **+32** |

It wins at every level with the cost overcharged. And the defect itself survives the cut:
54 of 60 mates at −20%, and **50 of 60 even at −30%**, against 27 before.

**Two things about how this was missed for the whole life of the project.** The tactics test
searched every position with 60,000 nodes — the level 4 budget — so no test had ever asked the
weak levels the question at the budget they play with. And self-play cannot see it either:
both sides share the blindness, so the games look balanced, exactly as §5.1's repetition
defect did. `tests/search.c` now checks mate in one at each level's *own* budget.

**The unmeasured part, stated rather than buried:** the 12.2% is a host figure. VICE is not
installed on the machine this was measured on, so `tests/c64search.c` was not run and the real
per-node cost on a C64 is unknown. The 20% charge above is an estimate scaled from the one
previous case where both numbers exist. Anyone with a C64 or VICE to hand should replace it.

## 5.2 The opening was dull, and the reason was mundane

From the starting position the engine played a knight to c3. Every time. Not a bad move;
simply always the same one, which made the first few moves of every game identical.

The cause was not mysterious. The evaluation frequently rates several opening moves exactly
equal, and ties fell to whichever move the generator produced first — a property of the loop
order, not of chess.

**This has been fixed twice over, and it cost the measurements nothing.** §6.11 of
`doc/engine.md` has the first mechanism: the randomiser perturbs move *ordering* at the root,
and ordering cannot change what alpha-beta returns except among moves that already score
exactly equal, so the move it plays is always one the search ranked equal-best.

That turned out to reach exactly two moves — `b1c3` and `g1f3` — because those are the only
two the evaluation rates equal-best, on every level and every seed. So the first move now comes
from a four-entry table instead (§6.10a): e4, d4, Nf3, c4. **That is not a concession to get
variety, it is an improvement.** Given its own 256-position opening set each and 512 games at
level 4 against Stockfish at 100 nodes, all four table moves outscore the move the engine
picked for itself, which finishes last of the five at −29 Elo.

The determinism this document depends on is preserved by a stronger mechanism than a flag:
the randomiser does nothing until it is seeded, and it is seeded by `main.c`, which is not in
the test build. Every harness in `tests/` therefore plays exactly the games it played before
the feature existed. The check that this is true rather than merely intended is that the
ladder reproduces to the digit across the change — level 4 against Stockfish at one node,
248–129–135 on both sides of it.

## 5.3 The other thing that did not happen

No illegal move was played across roughly 40,000 games.

That is worth stating because it was not guaranteed. The move generator had been verified
by node-counting to a fixed depth from a handful of standard positions — a strong test, but a
*shallow* one, exploring a few million positions near the start of a game. A real game is 70
to 150 moves deep and wanders into endgames, promotion races and fortress positions that a
depth-5 count never reaches. Forty thousand games is a very different kind of test, and the
generator passed it without a single complaint from either referee.

**Four games were lost on time, and none of them were the engine's doing.** They are in the
first anchor run, and the cause is in §4.3: the harness passed fastchess a 30 ms move limit
that an engine ignoring the clock had no reason to respect, and host load did the rest. The
limit is now 5000 ms. This sentence used to claim no game was ever lost on time, which was
true when written and stopped being true without anyone noticing — the flag that caught it is
printed by `gauntlet.py` on every affected rung, which is the only reason it is here rather
than silently inside a rating.

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

**The anchor rung is not reproducible**, and worse than that, its answer depends on which
rated rung it is read at — by 144 points at level 4 (§4.3). Stockfish's `UCI_Elo` scale is
compressed at this time control, so the engine looks stronger the stronger the opponent it is
measured against.

**A gain measured against node-limited Stockfish may not be a gain against anything else.**
Stockfish at 1 to 300 nodes does not search, so a change that is pure endgame knowledge is
worth more against it than against an opponent that plays endgames properly — and §5.1.3 shows
exactly that disagreement between the ladder and the rated anchor. Non-transitivity is the
third caveat in §4.3, and it is not hypothetical here.

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
5. Run the anchor separately, and treat it with the suspicion §4.3 earns it. Give each level
   the two rated rungs nearest its own strength rather than one rung for all four — a rung the
   engine scores 88% against carries almost no information — and run it at a concurrency below
   the core count, because it is the one measurement with a clock in it.

The whole ladder is roughly 8,000 games and takes a few minutes. **There is no reason ever to
run a short match.**

The build targets, script names and exact command lines live in `tests/`, which is where they
will stay current; naming them here would only date this document.

---

# Appendix A — The complete ladder

512 games a rung. Rating difference relative to the Stockfish setting, 95% interval, and the
opening-pair breakdown (both games lost, split, both drawn, split the other way, both won).
fastchess alpha 1.8.2, Stockfish 18, `tests/book.epd`, both sides node limited.

| Level | Opponent | W–L–D | Score | Diff | 95% interval | Pairs (0–2) |
|---|---|---|---|---|---|---|
| 1 | SF 1 node | 30–376–106 | 0.162 | −285 | [−317, −253] | 137,79,34,5,1 |
| 1 | SF 30 nodes | 27–398–87 | 0.138 | −319 | [−355, −282] | 157,66,25,7,1 |
| 1 | SF 100 nodes | 15–447–50 | 0.078 | −429 | [−474, −383] | 194,45,16,1,0 |
| 1 | SF 300 nodes | 8–483–21 | 0.036 | −570 | [−639, −501] | 227,21,8,0,0 |
| 2 | SF 1 node | 69–313–130 | 0.262 | −180 | [−208, −152] | 97,76,63,14,6 |
| 2 | SF 30 nodes | 67–314–131 | 0.259 | −183 | [−211, −155] | 97,76,65,13,5 |
| 2 | SF 100 nodes | 50–362–100 | 0.195 | −246 | [−277, −215] | 124,76,46,8,2 |
| 2 | SF 300 nodes | 14–448–50 | 0.076 | −434 | [−480, −387] | 197,41,17,1,0 |
| 3 | SF 1 node | 194–165–153 | 0.528 | +20 | [−6, +45] | 30,45,84,60,37 |
| 3 | SF 30 nodes | 184–175–153 | 0.509 | +6 | [−19, +31] | 32,46,85,67,26 |
| 3 | SF 100 nodes | 147–244–121 | 0.405 | −67 | [−92, −41] | 54,58,94,31,19 |
| 3 | SF 300 nodes | 78–360–74 | 0.225 | −215 | [−247, −183] | 126,54,56,16,4 |
| 4 | SF 1 node | 298–82–132 | 0.711 | **+156** | [+129, +184] | 5,25,65,71,90 |
| 4 | SF 30 nodes | 291–100–121 | 0.687 | +136 | [+109, +164] | 11,22,68,75,80 |
| 4 | SF 100 nodes | 211–183–118 | 0.527 | +19 | [−7, +45] | 28,47,91,49,41 |
| 4 | SF 300 nodes | 122–286–104 | 0.340 | −115 | [−143, −87] | 80,58,78,26,14 |

## A.1 The three earlier runs, kept for comparison

First column at each level, across the life of the project. §5.1.3 and §5.1.5 are what these
are for.

| Level | Original | After repetition | After endgame tables | Current |
|---|---|---|---|---|
| 1 | −429 | −422 | −361 | **−285** |
| 2 | −292 | −303 | −250 | **−180** |
| 3 | −111 | −91 | −31 | **+20** |
| 4 | +14 | +30 | +82 | **+156** |

The "after repetition" column is reconstructed from the deltas recorded in §5.1.2, which is how
that run was written down; the other three are measured tables. The cross-check against the
second runner was run against the endgame-tables engine and read −357, −254, −35, +77 against
fastchess's −361, −250, −31, +82; it has not been repeated since.

# Appendix B — Self-play reference

512 games, level-equivalent settings, engine against itself.

| | |
|---|---|
| Result | 97–97–318 — exactly level |
| Opening pairs | 256 of 256 scored 1–1 |
| Draws | 318 (62%), **all** by threefold repetition |
| Median game | 72 plies |
| Longest game | 151 plies |

This is the pre-fix reference, kept as it was measured — it is the evidence §5.1 is built on.
The equivalent run with repetition detection is in §5.1.1: still exactly level, as a
configuration against itself must be, but 162 draws instead of 272 over the harness's own
512-game set.

# Appendix C — On provenance

This engine was written with AI assistance, and so was this measurement. That is worth
stating plainly rather than leaving to be inferred, because a reader is entitled to know how
a thing was made when judging what it demonstrates.

It does not change what the numbers mean. Twenty thousand games were played by two
independent referees against a program nobody here wrote, and the harness was validated
against a result known in advance. Those checks exist precisely so that the answer does not
depend on trusting whoever — or whatever — assembled the question.
