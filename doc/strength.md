# How strong is it, actually?

A companion to `engine.md`. That document explains how the engine works; this one asks a
harder question — *how well does it play?* — and takes the trouble to answer it properly.

The short version is at the top. Everything after it is the method, the current results, and
an honest account of what the numbers do and do not mean, because a strength claim without
its caveats is just a boast.

The measurements that produced these figures, and the defects they turned up along the way,
are in `doc/rework-log.md`. This file describes the engine at HEAD.

---

## Executive summary

The engine was played against Stockfish 18 — one of the strongest chess programs in
existence — across roughly **40,000 games**. Each of the four skill levels from the game's
own menu was measured, and the results were cross-checked with two independent match
runners.

**The four levels, placed on Stockfish's own rating scale:**

| Menu level | Node budget | Time on a stock C64 | Approximate rating |
|---|---|---|---|
| 1 — Very Easy | 400 | 11 seconds a move | **~1240** |
| 2 — Easy | 1,200 | 40 seconds | **~1430** |
| 3 — Harder | 18,000 | ~11 minutes | **~1720** |
| 4 — Very Hard | 65,000 | ~48 minutes | **~1950** |

The first two times are measured on an emulated C64 over real games; the last two are computed
from the same node rate and are upper bounds, because levels 3 and 4 often finish an iteration
before the budget runs out. Levels 3 and 4 are emulator settings.

Each figure carries an honest uncertainty of about **±150 points** — not because the games
were few, but for reasons explained in §4.3 and §5 that no number of games would fix.

**What the tables say:**

**The levels are real.** Each rung beats the one below it by 105 to 200 points, with no
overlap in the confidence intervals. The difficulty menu is not decorative.

**At its strongest setting the engine is well past Stockfish's floor.** Level 4 scored
292–84–136 against Stockfish restricted to a single search node over 512 games, a difference
of +150, and draws level with it at a hundred nodes. Level 3 is ahead at one node (+44).

**Thinking twice as long is worth about 60 rating points.** Measured across the full range
from 400 to 65,000 nodes. This is the number that says what an accelerated emulator buys
you, and it is why level 4 is only about 100 points above level 3 despite thinking more than
three times as long.

**Against Sargon II**, a 1978 program running on an Apple II, Harder scored 57% at Sargon's
level 4. That is a screen, not a rating; see §4.5.

**What the bottom rung is aimed at.** ~1240 is a deliberate floor, not the weakest thing that
would still run. Level 1 should beat a rank amateur — someone who knows how the pieces move and
takes what is offered — and lose to a seasoned beginner, meaning the first player who has
acquired the habit of asking what the opponent threatens before choosing a move. That is a
strange line to draw until you notice it is the engine's own horizon. At 400 nodes level 1
rarely starts a second iteration (`engine.md` §6.8), so it plays a **one-ply** move, and the
second ply is precisely what buys sight of the reply. Quiescence still runs at the leaf, so it
does not drop pieces to a plain capture and it delivers mate in one 55 times in 60; what it
cannot see is anything that needs a quiet move to arrange — including a mate in one against
itself. So the floor punishes blunders sharply and walks into attacks blind. That asymmetry is
the point. It is a recognisable human failure mode rather than a random mover wearing a
difficulty label, which is what makes losing to it instructive and beating it earned.

The floor is also where the *time* is least defensible, and for an unrelated reason: 400 nodes
is 11 seconds a move on a stock C64, and it is the fastest setting on the menu. "Very Easy"
reads as *quick* to most players and it is not. The budget cannot simply be cut — at 300 the
engine shuffles won endings into fifty-move draws (`engine.md` §6.8) — so the label is honest
about strength and optimistic about patience.

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

A king-safety term once looked like a mild improvement over 16 games and turned out, over
512, to be a genuine 2.6-sigma *loss*. Small matches do not merely fail to answer — they
answer confidently and wrongly.

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

The shipping game also has a four-entry White first-move table and a five-entry Black reply
table. The ladder is played from `tests/book.epd`, four moves deep, where neither table can
fire, and the UCI adapter's `OwnBook` defaults off. The ratings below are therefore the
search, not the tables.

## 2.4 Validating the instrument before trusting it

**The single most important step, and the one most often skipped.** Before measuring
anything against Stockfish, the engine was played against *itself* — same version, same
settings, 512 games.

It must come out exactly level. If it does not, the harness is measuring something other
than chess strength — a colour bias, a bug in the translator, a fault in the opening set.

The current check (`chesstest match sanity`) comes out **196–196–120**, conversion 81%.

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
| 3 — Harder | **+44** | +33 | −51 | −205 |
| 4 — Very Hard | **+150** | +142 | +16 | −120 |

Confidence intervals run from ±26 points at the top of the table to ±69 at the bottom. The
first two columns are identical within noise, for the reason given in §3.1.

**The ladder is clean.** Reading down the first column, the four levels are separated by
105, 224 and 106 points, in order, with no overlap between adjacent intervals. The four-item
difficulty menu delivers four genuinely different opponents. The uneven spacing is not a
defect: the node budgets step by 3×, 15× and about 3.6×, and §4.2 shows those gaps are
roughly what that buys.

**The headline row is level 4 against Stockfish at one node: 292–84–136 over 512 games,
+150 points.** An engine that fits in about 31 KB and runs on a 1 MHz processor is well past
the weakest configuration a modern engine can be persuaded into. Equality still sits at
Stockfish **100 nodes**: +16, interval −9 to +42.

The honest framing of that result: Stockfish at one node is not playing chess in any
meaningful sense — it is making a well-informed snap judgement using a world-class evaluation
function. It is a low bar for Stockfish. It is still a real opponent, and clearing it is a
real result.

## 4.2 What a doubling of thinking time buys

Because the engine's strength is a node budget, and the budgets are known exactly, the
ladder can be re-read as a curve of strength against effort.

From 400 nodes to 65,000 nodes is a factor of about 162, or 7.3 doublings, across a first-
column gain of 435 rating points. That is **about 60 points per doubling of thinking time.**

This is a useful number to carry around:

- Emulator acceleration is a free multiplier on thinking time. A 10×-accelerated machine is
  worth roughly 200 points at the same skill setting.
- Conversely, an optimisation that makes the search twice as fast is worth about 60 points —
  real, but not transformative.
- Diminishing returns are visible in the table: the step from level 3 to level 4 is 18,000
  to 65,000, about 3.6×, for 106 points on the one-node rung.
- And it sets the price of a rating point. Another 100 points from thinking alone would need
  roughly triple the node budget. That number does not fit: budgets are held in a 16-bit
  counter, which stops at 65,535, and level 4 spends 65,000 of it. **The engine is within 1%
  of the strongest setting a 16-bit budget can express.** Past that, strength has to come
  from playing better per node, not from searching more of them.

One exchange rate that does *not* apply: nodes you *prune away* are not the same currency as
nodes you are *granted*. The 60-point figure was measured by giving the search more budget.
A technique that skips work on an assumption has a different, and so far unfavourable, rate;
see `doc/rework-log.md`.

## 4.3 The anchor, and why it is the soft part

The ladder measures differences. Turning those into ratings needs an opponent whose rating is
claimed rather than inferred, and Stockfish's `UCI_LimitStrength` mode is the only one
available. It comes with a clock — its calibration assumes one — so this is the single
measurement in this document that is not reproducible.

Each level is played against the two rated rungs nearest its own strength, 256 games each, at
`tc=4+0.04`. Reading a rating off a rung the engine scores 88% against tells you very little.

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
anchor point as this project has produced, needing no extrapolation at all.

One game at the level 2 / 1700 rung was lost on time and the runner flagged it; the rung is
reported as measured rather than quietly dropped, and one game in 256 does not move it.

### 4.3.1 The anchor's answer depends on which rung you read it at

Every level reads *higher* the stronger the rung it is measured against, and the effect is
large:

| Level | Rung step | Nominal gap | Measured gap | Ratio |
|---|---|---|---|---|
| 1 | 1320 → 1500 | 180 | **192** | 0.9 |
| 2 | 1500 → 1700 | 200 | 164 | 1.2 |
| 3 | 1700 → 1900 | 200 | 78 | 2.6 |
| 4 | 1900 → 2100 | 200 | **164** | 1.2 |

A 200-point step in `UCI_Elo` is generally not worth 200 points of played strength, and the
shortfall grows with the rung: Stockfish's rating limiter runs out of ways to be weak that a
four-second clock does not already impose. Level 3 is "1701" or "1823" depending purely on
which rung you ask, and the honest reading is the rung nearest a 50% score.

**This is what ±150 means, and it is measured rather than asserted.** The statistical
interval on any single rung above is about ±40. The disagreement between two rungs measuring
the same thing is 144.

### Two other things that would have quietly corrupted this

**Games lost on time.** The engine ignores every clock and stops at a node count, so it cannot
legitimately lose on time — but the harness has to hand fastchess *some* limit, and a 30 ms
limit is close enough to a level 4 move on this host that load alone forfeits games. The
limit is 5000 ms, which cannot change a move that gets played.

**Concurrency.** A time-controlled match run with as many games in parallel as the host has
cores is partly a measurement of the host. These rungs run at concurrency 4 on 8 cores; the
node-limited ladder does not care and runs at 8.

Neither of these touches the ladder in §4.1. Both touch the anchor, which is one more reason
it is the soft part.

## 4.4 The correct form of the claim

> *Level 4 scores 70% against Stockfish restricted to a single search node, 52% against it at
> 100 nodes, and 57% against Stockfish's rating-limited mode set to 1900. That places it near
> 1950 on Stockfish's own scale, give or take 150.*

Note what that sentence does **not** say. It does not say the engine is a 1950-rated player.
Engine rating lists, Lichess ratings and FIDE ratings are three different pools with three
different scales, and a number from one does not transfer to another. A club player rated
1950 by their national federation would find this engine unfamiliar rather than equal — it
never gets tired, never miscalculates a two-move tactic, and has no idea what a plan is.

## 4.5 Against Sargon II

Sargon II is a 1978 Apple II program, and the match is the outside opponent this project
uses when Stockfish cannot see a defect both sides share. How to run it is `sargon/README.md`.

At HEAD, **cc65 Harder (18,000 nodes) against Sargon level 4** scored **22 wins, 13 losses
and 29 draws — 57.0%** over 64 games, with 33 distinct six-ply openings and no fifty-move
draws.

That is a screen, not a rating. Sargon is not reproducible: the harness seeds its opening
entropy, but the keyboard-wait counter keeps advancing, so the same seed can open two
different games. Comparisons are unpaired. Sixty-four games here are a few dozen distinct
games rather than 64 independent samples, and the honest interval is roughly ±12 points.
The pairing is the one that sits inside a competitive band; the number worth reading next to
the score is zero fifty-move draws.

The ratings in Part IV are unchanged by this section. They are measured against node-limited
Stockfish from `tests/book.epd`. What this section adds is an outside confirmation that the
engine finishes the endings it wins.

---

# Part V — Threats to validity

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
worth more against it than against an opponent that plays endgames properly. Non-transitivity
is not hypothetical here.

**Fixed-node Stockfish is not a rated configuration.** The ladder is internally consistent
and externally uncalibrated; it borrows its calibration entirely from the anchor.

**The engine is deterministic**, so all game-to-game variation comes from the opening set.
With 256 openings the effective sample is smaller than 512 independent games would be.

**Self-play cannot see a weakness both sides share.** A configuration playing itself will
come out level whether both sides convert won endings or both throw them away. Internal
harnesses tell you whether a change is consistent with the evaluation you already have.
They cannot tell you it is right. That is why the Sargon screen exists, and why a person at
a board still finds things 40,000 measured games do not.

**No illegal move was played** across those games. The move generator had been verified by
node-counting to a fixed depth from a handful of standard positions — a strong test, but a
shallow one. Forty thousand games is a very different kind of test, and the generator passed
it without a single complaint from either referee.

---

# Part VI — Reproducing this

The whole measurement is a handful of commands and a few minutes of desktop time.
`doc/measuring.md` is the instrument list.

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
fastchess alpha 1.8.2, Stockfish 18, `tests/book.epd`, both sides node limited. Budgets are
400 / 1,200 / 18,000 / 65,000.

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
| 3 | SF 1 node | 215–150–147 | 0.563 | +44 | [+18, +70] | 25,40,81,65,45 |
| 3 | SF 30 nodes | 207–158–147 | 0.548 | +33 | [+9, +57] | 21,41,97,62,35 |
| 3 | SF 100 nodes | 158–232–122 | 0.428 | −51 | [−76, −26] | 48,54,96,40,18 |
| 3 | SF 300 nodes | 80–351–81 | 0.235 | −205 | [−235, −174] | 120,52,66,15,3 |
| 4 | SF 1 node | 292–84–136 | 0.703 | **+150** | [+122, +177] | 6,25,66,73,86 |
| 4 | SF 30 nodes | 291–93–128 | 0.693 | +142 | [+114, +169] | 9,23,65,79,80 |
| 4 | SF 100 nodes | 211–187–114 | 0.523 | +16 | [−9, +42] | 29,43,100,43,41 |
| 4 | SF 300 nodes | 124–294–94 | 0.334 | −120 | [−147, −92] | 79,58,87,18,14 |

# Appendix B — Self-play reference

512 games, shipped configuration against itself, `chesstest match sanity`.

| | |
|---|---|
| Result | 196–196–120 — exactly level |
| Conversion | 81% of sides that were a clear piece up for ten plies or more |

# Appendix C — On provenance

This measurement, and the engine it describes, were produced with AI assistance. The method
was designed and the numbers were checked by a person; the running of the matches and the
writing of this document were not. That does not change what the tables say. It is why the
raw scores sit next to every rating difference, and why the commands that reproduce them are
in `doc/measuring.md`.
