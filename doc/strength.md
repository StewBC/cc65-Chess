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
| 1 — Very Easy | 400 | 13 seconds a move | **~1240** |
| 2 — Easy | 1,200 | 46 seconds | **~1430** |
| 3 — Harder | 15,000 | ~11 minutes | **~1700** |
| 4 — Very Hard | 60,000 | ~45 minutes | **~1950** |

The first two times are measured on an emulated C64 over real games; the last two are computed
from the same node rate and are upper bounds, because levels 3 and 4 often finish an iteration
before the budget runs out. **These are much longer than earlier versions of this table said**,
and the reason is §5.1.5: three accepted changes have made a node about 60% dearer between
them. Levels 3 and 4 are emulator settings and always were.

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
at captures even when the king was in check. §5.1.5 has the fix: +82, +49 and +31 Elo at levels
2, 3 and 4 at equal time, and at level 1 a wash on rating that nevertheless doubles the mates it
can find. A defect fix is not always a rating gain, and it is worth having anyway.

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

**And at equal time, which is the number that counts.** The per-node cost was measured twice.
On this host, over identical work, it is 12.2%. **On a real C64 it is 22.7%** — measured with
`tests/c64evasion.c` under `c64m`, which replays a *fixed* game so both builds walk identical
positions: 27.6 nodes/sec without evasions, 22.5 with. The host understated the target by
1.86x, almost exactly the 1.64x §5.1.1 found for the position hash, which is the third time
that lesson has been paid for.

Charged at the measured 22.7% — the fixed engine given proportionally fewer nodes, its opponent
unchanged:

| Level | budget, on vs off | vs SF 1 node | vs SF 100 |
|---|---|---|---|
| 1 | 326 v 400 | +15 | −20 |
| 2 | 978 v 1,200 | — | **+82** |
| 3 | 12,225 v 15,000 | — | **+49** |
| 4 | 48,900 v 60,000 | — | **+31** |

**Levels 2, 3 and 4 gain clearly. Level 1 is a wash** — +15 at one rung and −20 at the other,
both inside their own intervals. That is the honest result and it is worth stating plainly:
at the weakest setting, the depth given up to pay for evasions is worth about what the evasions
buy, *as measured in Elo against Stockfish*.

**Which is not the same as the change being worthless there.** What level 1 gets is the ability
to finish a game: 54 of 60 mates in one at the reduced budget against 27 before. A rating
difference against Stockfish at a hundred nodes does not measure "can it deliver mate when it
has one", and for the level a beginner actually plays, that is the property that matters. This
was a defect fix, and the rating table is not the instrument that judges it.

**Two things about how this was missed for the whole life of the project.** The tactics test
searched every position with 60,000 nodes — the level 4 budget — so no test had ever asked the
weak levels the question at the budget they play with. And self-play cannot see it either:
both sides share the blindness, so the games look balanced, exactly as §5.1's repetition
defect did. `tests/search.c` now checks mate in one at each level's *own* budget.

**What the on-target measurement also revealed:** the engine is a good deal slower on bare
metal than this document has been claiming. Level 1 now takes **13.2 seconds a move** measured
over a real game (`tests/c64level1.c` under `c64m`), against the 8.2 seconds recorded when the
skill budgets were set. That is the accumulated cost of the position hash, the endgame tables
and now check evasions — each individually affordable, and about 60% together. The time column
in the executive summary is updated; levels 3 and 4 have not been re-measured directly, because
a single level 4 move is now around three quarters of an hour on a stock C64.

## 5.1.6 Every draw was a win it did not finish

The second defect found by playing rather than measuring, and the second one this document had
been in a position to see and did not.

The engine was played against **Sargon II on an Apple II**, a 1978 program, and scored 21%
against Sargon's *level 1* — the second-weakest of its seven settings. The obvious reading is
that the ratings in Part IV are too generous. It is not what the games say.

Running the material balance over all twelve games of that batch gives one fact that settles
it: **every single game the engine drew, it was a clear piece or more up.** Not one draw was a
balanced position. Playing White it reached a clean king and rook against a bare king on move
66, still had it on move 115, and drew by the fifty-move rule. In another it reached king and
queen against king and pawn on ply 129 and let the pawn promote.

The entire margin between 21% and an even score was sitting in endings already won.

### The cause, and why every instrument here missed it

`sc_pstKingEnd` sends a king to the centre once the queens are off. It is a per-piece table, so
it says that to **both** kings, including the one being mated — which belongs on the edge. With
bare kings every rook move therefore scored identically. The engine had no gradient and
wandered.

Nothing in `tests/` asked the question. The ladder and the anchor both measure W-L-D, and
Phase 8 had already written down why that is blind here: *an engine can score dead level and
still turn won endings into draws.* The conversion metric built in Phase 8 could see it and did
— 79% of clear advantages converted, with 36 of the failures still a piece up at the end — but
79% had been read as "good and improving" rather than as "one game in five thrown away".

**What was missing was a test that asks for a mate rather than a result.** `chesstest convert`
is that test now: thirteen basic won endings, the engine defending itself, counting mates before
the fifty-move rule rather than wins.

### What the fix was worth — first version, superseded below

The figures here and in the next section were taken before the term met an outside opponent.
They are kept as they were taken because which of them survived is the point; the shipped
numbers are under "The second gate".

| Level | conversions before | after |
|---|---|---|
| 1 — 400 nodes | **5 / 13**, mean 35 plies | 12 / 13, mean 32 |
| 2 — 1,200 | 11 / 13, mean 40 | **13 / 13**, mean 30 |
| 3 — 15,000 | 8 / 13, mean 28 | **13 / 13**, mean 23 |
| 4 — 60,000 | 13 / 13, mean 20 | 13 / 13, mean 19 |

**Level 3 scored below level 2 before the change**, which is the clearest possible statement of
the disease: searching deeper into a flat evaluation finds more equally-scored ways to wander.

Against an opponent that defends properly — 100 random won endings with Stockfish holding the
weak side at fixed depth, so the defence is identical across builds:

| | before | after |
|---|---|---|
| level 1 | 42 / 100 | **75 / 100** |
| level 2 | 61 / 100 | **75 / 100** |

King and queen against a bare king went from 18 of 25 at a mean of 51 plies to 25 of 25 at 17.
Level 1 now converts as well as level 2, which is what knowledge substituting for search looks
like — the same shape Phase 9 found for the endgame tables.

On the 512-game self-play match, same configuration both sides: conversion **79% → 85%**, drew
still a piece up **36 → 14**, stalemate draws **6 → 0**, total draws 126 → 100.

### The first version of the fix lost the match, and only Sargon could tell

Everything above was measured inside this repository. Played against **Sargon II** — the
opponent whose games started the section — the fix was a disaster: 32 games at the settings of
the pre-fix baseline took **51.6% to 31.2%**. The fifty-move draws went to zero exactly as
designed, and became losses.

The term was not the problem; the gate was. It fired inside `gePhase < PHASE_ENDGAME`, and
that constant is 3200 — two rooks and two minors still on the board. Asking the two binaries
what they made of the position where one game parted from its baseline, at move 34:

```
pre-fix : score cp -463  bestmove Kc2
post-fix: score cp -561  bestmove b4
```

cc65 was a piece **down** there. The `|score| > 400` gate was written to ask "am I winning" and
it also answers "am I losing".

**Every instrument in this document missed it.** `chesstest convert` and the Stockfish endgame
benchmark hold nothing but bare-king endings, all below gePhase 1000, so neither ever exercised
the term outside its design range. `match sanity` and `match drive` are self-play, where both
sides carry the term and the harm cancels — and self-play conversion read **85%** for the
broken version against **81%** for the fixed one. *The best number in the file belonged to the
version that lost.*

§2.4 says an instrument has to be validated before it is trusted, and Part VI says a self-play
number is a reason to go and measure rather than a result. Both were already written down. It
still took an outside opponent to apply them.

### The second gate, and what survived it

`DRIVE_PHASE` at 1100, set just above what has been measured rather than at a guess: king and
rook is 500, bishop and knight 660, queen 900, two rooks 1000 — and the position that caused
the regression is 1650. Queen against a lone minor is 1220 and is deliberately *not* covered,
because nothing here has measured it and the first version is what a gate set by reasoning
rather than measurement costs.

| | pre-fix | gate on score alone | **shipped** |
|---|---|---|---|
| `chesstest convert`, levels 1-4 | 5/11/8/13 | 12/13/13/13 | **12/13/13/13** |
| Stockfish, 100 won endings, level 1 | 42 | 75 | **75** |
| Stockfish, 100 won endings, level 2 | 61 | 75 | **75** |
| self-play conversion | 79% | 85% | **81%** |
| **Sargon II, first 32 games** | **51.6%** | **31.2%** | **57.8%** |

The shipped version then ran the full 64, against the pre-fix baseline of the same size and
settings:

| Sargon II, 64 games | pre-fix | shipped |
|---|---|---|
| W-L-D | 10W 19L 35D | **27W 20L 17D** |
| score | 42.97% | **55.47%** |
| fifty-move draws | 15 | **2** |
| conversion | 6 of 23 (**26%**) | 26 of 28 (**92%**) |
| drew still a piece up | **15** | **0** |
| cc65 as White | 9W 3L 20D — 59.4% | **25W 2L 5D — 85.9%** |
| cc65 as Black | 1W 16L 15D — 26.6% | 2W 18L 12D — 25.0% |

Fifteen games ended still holding a rook against a bare king; now none. Twenty White draws
became five. Black does not move and should not: a term that helps finish won positions does
nothing for a side that is rarely winning.

**The percentage on its own is not significant and should not be quoted as though it were.**
Naive 95% intervals are 30.8–55.1% and 43.3–67.6%, overlapping, over an effective sample of 16
to 17 distinct games rather than 64. The conversion count and the fifty-move column carry the
argument, because they are categorical outcomes tied to the mechanism.

**The colour split is the more interesting number.** Sargon scores 75.0% with White and cc65
now scores 85.9% with it, so on equal footing cc65 is ahead — but sixty points between colours
is not first-move advantage.

This paragraph used to continue "it is an opening-book gap", and go on to say that the
remaining half of the deficit was an opening problem. **That was a hypothesis with nothing
behind it, and §5.1.7 is what happened when it was finally checked.** It is wrong in its
mechanism and right by accident in its conclusion, which is the least useful way for a claim
to be half true. It is left standing here, struck through rather than quietly rewritten,
because this document has a rule about unmeasured explanations and this is the second time in
two sections that the rule caught its own author.

### What this rig can and cannot say

**Sargon is not reproducible.** The harness seeds `$4E` before the level is typed, but the
keyboard-wait counter keeps advancing, so its opening book depends on host timing: the same
seed gave `1.d4 Nf6` in one run and `1.d4 d5` in the next, with cc65's own moves identical. No
paired replay is possible and every figure here is unpaired.

And **64 games is 17 distinct games**, four of them on the Black side. The fifteen fifty-move
draws in the pre-fix baseline were one game played fifteen times. Percentages from this rig
move in large steps, which is why the colour split and the termination counts carry the
argument above and the percentage does not.

### What it costs on a 6502

The host cannot see this term: 1,500 searches of 60,000 nodes from endgame positions, the term
firing at every node, came to 6.80s without and 6.63s with — smaller than the run-to-run spread.

Neither existing on-target benchmark could price it either. `c64search.c` runs from the opening,
where gePhase is 6400 against the term's bound of 1100, so it would have reported zero
faithfully and uselessly; `c64evasion.c` has the right shape but replays a middlegame, because
check evasions cost nothing where nobody is in check. `tests/c64drive.c` is the instrument that
could: `c64evasion.c`'s fixed replay with the positions inverted, running the pre-fix Sargon
game that reached king and rook against a bare king and searching only from where gePhase falls
to 1100. Both halves are the shipping configuration built twice, since `EVAL_TUNING` makes every
node dearer and a node's cost is what is being measured.

On a real C64 under VICE, identical positions in both builds: **43.225 nodes/sec without the
term and 42.361 with, so a node is 2.04% dearer.** That is the cost where it applies; in a
middlegame it is zero by construction. Check evasions cost 22.7% by the same method.

**Charged at 2%, it still wins** — `match drive` at equal time, 2,940 nodes against 3,000, comes
to 246-238-28 where equal nodes gave 245-240-27. That makes it the second evaluation term in the
project to survive the equal-time test and the first to survive it comfortably; pawn structure
and the endgame king table both died there.

**The ratings in Part IV are unchanged by this section.** They are measured against node-limited
Stockfish, which does not reach these endings, and Phase 9 already established that a change
which is pure endgame knowledge is worth far more against opponents that do not search endgames
than against ones that do. What this section changes is a different claim: the engine finishes
what it wins.

## 5.1.7 Twenty-five percent as Black was one lost game, played fourteen times

The colour split above is the largest number in this document that nobody had taken apart.
Taking it apart took an afternoon and found that almost none of it means what it says.

### The counting, which needed no engine at all

Sorting the thirty-two Black games of the shipped run by their opening:

| cc65 as Black | n | result |
|---|---|---|
| `1.e4 Nc6` | 14 | 0W **14L** 0D — **0%** |
| `1.d4 Nc6` | 10 | 0W 0L 10D — 50% |
| `1.c4 d5` | 4 | 0W **4L** 0D — **0%** |
| `1.Nf3 Nc6` | 2 | 50% |
| `1.f4 Nc6` | 2 | 100% |
| **everything except the two 0% lines** | **14** | 2W **0L** 12D — **57.1%** |

All eighteen losses are in two lines. Outside them Black does not lose a game and scores 57%.
And the fourteen `1.e4` games are not fourteen games: they are **identical for all 103 plies**,
mate included. Sargon is not reproducible in general (§5.1.6), but in that line it was, so
one loss went into the average fourteen times.

Twenty-five percent is therefore mostly a weighting. The Black side of a 64-game run holds
**five distinct games**, and the largest of them is a loss.

### The opening is not where those games were lost

The obvious next claim — the losses come from a bad opening, so a book fixes them — is the one
that was already assumed, so it got checked rather than repeated. Running Stockfish over the
`1.e4 Nc6` game move by move:

```
move 10  Black −136 cp     a normal, slightly worse position
move 13  Black −181 cp     Black is winning
move 17  ...Bd6  +491 cp   the game is thrown here
```

Sargon's `11.Nxf7`, the sacrifice that looks like the refutation of the opening, is **worth
−139 to Sargon** — objectively bad and practically winning, which is the difference between
those two words at 1,200 nodes. The `1.c4` game has the same shape: Black at −115 on move 11,
then twenty moves of drift. Both losing lines leave the engine equal or better out of the
opening and lose in the middlegame.

### There is no colour asymmetry to explain

The engine plays `tests/book.epd` positions with both colours. Those start after four moves,
so no book fires for either side and any gap is the engine itself. 256 openings played both
ways, against Stockfish at 30 nodes:

| | cc65 as White | cc65 as Black |
|---|---|---|
| level 2 | 39.6% | 33.6% |
| level 1 | **28.1%** | **28.1%** |

Level 1 is level to the digit; the level-2 gap is 6 points at about 3% standard error, which is
first-move advantage and noise. **The engine is not weaker with Black.** Sixty points is not
coming from anything the search does.

What is left is the weighting. White has a four-entry table, so 32 White games hold twelve
distinct openings and no single game can dominate. Black had no table, so 32 Black games hold
five, and one of them is a 103-ply loss counted fourteen times. **The book helps White by
spreading the sample, not by improving the moves** — which is the same claim §5.2 makes about
the White table's own value, arrived at from the other end.

### The instrument could not play the feature

Then the reason nobody had noticed: **until this section, nothing in the repository could reach
the opening table at all.** `tests/uci` calls `search_Best` directly and never goes through
`cpu_Play`, and nothing in `tests/` calls `search_SetSeed`, which the table is gated behind. The
Sargon harness needed White's opening to vary, so it had **a copy of the table written out
again in Python**. Every figure in Parts IV and V was measured with the book off, and the only
thing that had ever played it was the shipping 8-bit game.

`tests/uci` now has `OwnBook` and `BookSeed`, both off and inert by default so every game in
this document still reproduces, and `sargon/match.py` asks the engine instead of its own copy.
A table that only one un-instrumented binary can execute is not a feature, it is a rumour.

### The table, and how its entries were chosen

Black now answers White's first move from a table, on the same terms as White's: five entries,
two replies each, anything else falls through to the search. Every entry was picked by playing
it — 192 openings a reply, cc65 as Black at 1,200 nodes, against Stockfish at 30 and at 100
nodes:

| | best reply | the move it played before |
|---|---|---|
| 1.e4 | **d5 38.0%** | Nc6 30.3% |
| 1.f4 | **e5 39.1%** | Nc6 31.4% |
| 1.d4 | Nf6 34.6% | Nc6 32.8% |
| 1.c4 | d5 34.0% | Nc6 32.8% |
| 1.Nf3 | d5 32.8% | Nc6 31.5% |

**Three of the five are flat, and that is the finding rather than a disappointment.** Which
reply gets played barely matters. The table is not buying opening theory; it is buying ten
distinct Black games where there were five.

Measured as a whole, against an outside opponent, in a paired design — White's first move
forced through the five Sargon plays, Stockfish continuing from there at twenty different node
budgets, so each book-off game has book-on games from the same opening against the same
opponent:

| cc65 | book off | book on | paired difference | |
|---|---|---|---|---|
| level 1 | 25.0% | 28.0% | +3.0% | t = +1.00 |
| level 2 | 22.0% | 32.0% | **+10.0%** | **t = +3.29** |
| level 2, replication | 15.5% | 28.5% | **+13.0%** | **t = +4.18** |
| level 3 | 43.0% | 47.8% | +4.8% | t = +1.18 |

Positive at every budget, over 100 pairings each. The level-2 row was run twice on **disjoint
sets of opponent node budgets** — a replication rather than a re-reading of the same games —
because a *t* computed over twenty settings of one opponent is treating correlated games as
independent ones, and the honest check on that is to do it again somewhere else. It held: +10.0
then +13.0, and 55 openings helped against 17 hurt on the second run.

The per-opening breakdown disagrees with itself between levels — `1.d4` is +23.8% at level 2
and −16.2% at level 3 — which is what a variety effect looks like and not what a better-move
effect looks like. Read the whole thing as "not worse, and probably better at the level a
person actually plays", and let the rig carry the rest.

### What the rig said, including the part that went wrong

cc65 as Black in every game — `sargon/match.py --cc-color black`, which exists because a colour
question measured with alternating colours spends half its games on the other side. Three runs,
against the shipped run's 32 Black games:

| cc65 as Black vs Sargon L1 | no table | table, `1.e4` alt `d6` | table, `1.e4` alt `e5` |
|---|---|---|---|
| games | 32 | 64 | 32 |
| W–L–D | 2W 18L 12D | 18W 34L 12D | **10W 13L 9D** |
| score | 25.0% | 37.5% | **45.3%** |
| **distinct games** | **5** (16%) | 26 (41%) | **24 (75%)** |
| largest single game repeated | **14×** | 12× | **5×** |

**The distinct-game column is the result and the score is the by-product.** Three quarters of
the final run's games are their own game, against one sixth before; the worst repeat is five
where it was fourteen. The score moved twenty points with it, over an effective sample that
went from about five games to about twenty-four — which is the first time any figure from this
rig has had a sample worth quoting.

The middle column is why there are three runs and not two, and the per-entry breakdown is where
this section earns its place:

| | n | result | distinct games |
|---|---|---|---|
| `1.e4 d5` | 15 | 3W 7L 5D — 36.7% | **11** |
| `1.e4 d6` | 21 | 0W **21L** 0D — **0%** | **2** |
| `1.d4 Nf6` | 12 | 12W 0L 0D — 100% | **1** |
| everything else | 16 | 3W 6L 7D | 12 |

**`1.e4 d6` reproduced the exact defect the table was built to remove.** Twenty-one games, two
distinct, every one of them a 105-ply loss. `1.d4 Nf6` is the same shape with the sign flipped:
twelve wins that are one game. Only `1.e4 d5` behaves the way the whole table was supposed to,
and the reason is visible in the moves — `2.exd5 Qxd5` forces a capture and a recapture, the
queen comes out, and Sargon's replies diverge from there. The closed replies let both programs
replay the same game.

**Neither desktop instrument predicted this, and one of them was built specifically to.** On
score, `d6` ranked second of eight replies to 1.e4 (33.6%). So a second measure was written —
count *distinct games* rather than score, perturbing the opponent's node budget to stand in for
a real opponent's own book — and `d6` came top of that too, 13 distinct games of 16, the best of
any reply. Against Sargon it produced two.

That is §4.3's non-transitivity warning arriving in a form nobody had allowed for. It is not
only that a *score* against one opponent fails to transfer; the *variety* a reply produces
fails to transfer as well, because variety against Sargon comes from Sargon's book and search
diverging and nothing on this desk perturbs those. **Two proxies, both reasonable, both wrong,
and the rig was the only thing that could tell.**

The `1.e4` alternative is now `e5` rather than `d6`, chosen on the one mechanism the games
actually showed — a reply that forces an exchange — and confirmed on the rig rather than on a
proxy, because the proxies have now failed twice:

| `1.e4` and the reply | n | result | distinct games |
|---|---|---|---|
| `Nc6`, before any table | 14 | 0W 14L 0D — 0% | **1** |
| `d6` | 21 | 0W 21L 0D — 0% | **2** |
| **`e5`** | 5 | 1W 2L 2D — 40.0% | **5** |
| `d5`, both runs | 23 | 5W 11L 7D — 37.0% | 18 |

**Read the right column.** `e5` is five games and 40% and that percentage means nothing at
n=5; what means something is that five games produced five games. `d5` and `e5` between them
now behave the way the entry was specified to, and the 0-of-21 hole is closed.

Two things are deliberately left alone. `1.d4 Nf6` still wins twelve of twelve as one repeated
game — changing it would mean trading a 100% entry for a proxy that has failed twice, so it is
flagged rather than fixed. And the table answers five first moves because Sargon plays five; a
human plays others, and every one of those still falls through to the search and to whatever
single reply it produces.

### What it costs

253 bytes of code and data. The Apple II went from 1419 free to 1166; the Atari paid 256
because those 253 bytes crossed the `DLIST` page boundary for the third time in this project,
and has 716 left. Nothing else is close to a limit. No node is dearer — the table replaces a
search rather than adding to one, so unlike every term in Part V this one is not charged at the
board at all.

**The ratings in Part IV are unchanged, and cannot be otherwise.** Every rung is played from
`tests/book.epd`, four moves deep, where neither table can fire; and `OwnBook` defaults false,
so the adapter those games were measured through still plays them. `match sanity` reproduces
196-196-120 and 81% conversion to the digit across this change, which is the check that the
harnesses really are seeing the engine they saw before.

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

## 5.2a Six games against Sargon at level 6

Everything else in this document that involves Sargon II is played against its **level 1**, the
second-weakest of seven. This is the only look so far at what happens when both programs are
set to maximum: cc65 at Very Hard, 60,000 nodes, against Sargon level 6.

**Six games is not a measurement and no rate below should be read as one.** It was run out of
curiosity, three games with each colour, and it is written down because a handful of the things
it found are *categorical* — existence claims that do not need a sample size — and because the
hypothesis it produced did not survive being measured properly, which is the more useful half.

```
cc65 2 wins, 3 losses, 1 draw   (41.7%)
6 games, 6 distinct openings, 3 with each colour
8.4 hours, mean 84 minutes a game, about 45 seconds per Sargon move at max turbo
```

### What this establishes, sample size notwithstanding

**cc65 at Very Hard beats Sargon at level 6.** Twice. That is an existence claim and six games
settle it.

**Sargon at level 6 misses forced mates.** It had mate on the board and did not play it in
three separate games — twice in one of them, where cc65 walked into a forced mate at move 30,
was let off, walked into another at move 38, and was let off again. A 155-ply win and a 191-ply
draw are on the board partly because of this.

The likely reason is the same defect class this project keeps finding in itself. At 45 seconds
a move under emulation, level 6 on period hardware was hours a move — so it is the setting
Sargon's authors could least afford to play, and therefore the least exercised path in the
program. `AGENTS.md` states the rule as *a test that runs at one budget has not tested the
skill levels*, and Sargon's deepest level and cc65's mate blindness at levels 1 and 2 (§5.1.5)
are the same failure forty-eight years apart.

**cc65 is outplayed positionally in the games it wins.** Over the first twenty moves its
evaluation falls by roughly the same amount whether it goes on to win or lose — −773 and −693
in the two wins against −683 and −780 in two of the losses. The results came from Sargon's
errors, not from cc65 playing better. Against level 1 the engine was reaching move 13 *ahead*
and losing it later (§5.1.7); here it is behind by move 13 in every game.

### A hypothesis, and what happened when it was measured

Two of the first three games turned on an early queen sortie — `10.Qb5` walking into a
four-move trap, `10...Qc6` costing 414 centipawns — and the third, a win, never moved the queen
at all. Counting queen moves in each game's first twenty gave an unusually clean table: **the
three losses had four each, the draw two, the wins zero and one.**

There is even a mechanism. `sc_pstQueen` in `eval.c` is a plain centralization table: d1 scores
−5 and the middle squares +5, so **the evaluation pays the queen ten centipawns to leave home**,
nothing anywhere penalises developing it early, and the punishment in game 1 took four
preparatory moves — comfortably past the depth 5 to 6 that 60,000 nodes reaches.

Correlation with the result is a bad statistic, though, because a losing position invites queen
moves as much as queen moves invite a losing position. The better one is what a move of each
kind actually costs, over every cc65 move of all six games:

```
queen   25 moves   -70.9 cp/move
rook    24 moves  -134.4 cp/move        <- twice as expensive
```

**Which refuted it.** Except that piece type is confounded with phase — rooks hardly move
before move 15 — and splitting on that rescues the hypothesis in a sharper form:

| moves 1–15 | cp/move | moves 16–30 | cp/move |
|---|---|---|---|
| **queen** | **−85.5** (14 moves) | rook | −145.8 (21) |
| rook | −55.0 (3) | knight | −99.8 (11) |
| bishop | −35.2 (18) | queen | −52.3 (11) |
| knight | −22.1 (23) | bishop | −48.2 (23) |
| pawn | −20.1 (28) | pawn | −48.2 (9) |

**In the opening the queen is the most expensive piece cc65 can move**, at four times the cost
of a pawn or a knight, and by the middlegame it is unremarkable. That is a real claim with a
mechanism and a candidate fix that costs *nothing* — the table is already 64 bytes of RODATA
looked up at `eval.c:278`, so changing its numbers is free on every target, including the
Atari's 716 remaining bytes.

**It is still not established, and two things stop it.** Fourteen queen moves across six games
is a small sample. And Stockfish penalising an early queen sortie is close to definitional —
it holds opening principles the same way a textbook does, so its agreement is weaker evidence
than it looks. The only test that would settle it is an `EVAL_TUNING` switch on the queen
table, A/B'd at **equal time** over hundreds of games against Stockfish, and then confirmed
against an outside opponent, because §5.1.7 is the section where a Stockfish ranking failed to
transfer to Sargon on both of the measures tried.

Recorded as the most promising untested lead in the project, not as a finding.

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
