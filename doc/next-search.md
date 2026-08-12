# Next: the search, and how to know it worked

A work note for whoever picks this up next, human or agent. Read `AGENTS.md` first, then
`doc/measuring.md` §3, then this.

**Status: the search-technique portfolio is closed empty.** Four pruning candidates were
measured and reverted in `doc/rework-log.md` Phases 14 to 18. The remaining exact technique,
the transposition table, stopped before implementation in Phase 19: widening the position hash
cost 15.3% on a C64, above the 10% human-decision gate. The shipping engine is unchanged.

---

## 1. What is in the search, and what has been tried

`src/search.c` already has **iterative deepening** with the previous best tried first, **killer
moves**, **MVV-LVA** capture ordering, **`pickBest`** selection rather than sorting,
**quiescence** with stand-pat and check evasions, **repetition and fifty-move detection inside
the search**, a **node budget** with a clean abort that banks a legal move before searching it,
and **randomised tie-breaking** for opening variety.

Its move ordering is already close to minimal — `AGENTS.md` records about 2.28 moves tried per
node before a cutoff — and that fact turns out to explain most of what follows.

**Tried, measured and reverted (2026-08-11):**

| candidate | best node saving | outcome |
|---|---|---|
| principal variation search | 5–6% at levels 3–4, *worse* at 1–2 | below the instrument's resolution |
| delta pruning | 3.2% | stopped at the pre-gate |
| losing-capture skipping | 13.7% | stopped at the pre-gate |
| **null move** | **27.0% / 21.8%** at levels 2 / 4 | cleared the node gate, then measured **+0.04σ** |

**Still untried:** history heuristic, SEE, late move reductions, aspiration windows.
**Tried since:** one-ply check extension (E5 / Phase 32) — rejected; L1 mate-in-one fell below
its floor and Atari took a page. The portfolio result still gives none of the untried list a
reason to move ahead of exact-speed leftovers or E2/E3.

## 2. What the portfolio established — read this before proposing anything

The original version of this note asserted that because strength here *is* a node budget, any
technique that reaches deeper within the same budget is a direct win, and it set a pre-gate of
roughly 20% node savings on the arithmetic of 60 Elo per doubling. **That premise is wrong, and
null move is the experiment that proves it.**

Null move saved 27% and 22% of nodes at the levels it targeted — clearing that gate
comfortably — and produced a pooled score difference of +0.0003, about **0.04 sigma**, missing
the landing bar by roughly fiftyfold.

The error is in the exchange rate. **The 60-Elo-per-doubling curve was measured by *granting*
the search more nodes** (`doc/strength.md` §4.2). Nodes you are given are spent searching at
full fidelity. Nodes you *prune away* are removed on an assumption that is sometimes wrong, so
the extra depth you reach is lower-fidelity depth. The two are different currencies and only
the second has a measured rate against Elo. Saved nodes are not granted nodes.

The other three results have a second, compatible explanation: **when move ordering is already
near-minimal there is very little tree left for a pruning technique to take.** PVS trades on
window inefficiency and delta pruning on quiescence bloat, and neither was there in quantity.

Two rules follow, and they are the durable output of the whole exercise:

1. **The node pre-gate is a floor, not a predictor.** Passing it means "large enough to be worth
   measuring". It says nothing about whether the saving becomes strength. Three candidates were
   correctly stopped by it for pennies; the fourth passed it and still measured nothing.
2. **Nothing that prunes on an assumption should be expected to convert.** A technique whose
   saved work is *exact* — a transposition table returning a completed search — was a different
   proposition; §3a records why its prerequisite cost stopped it before the table itself could
   be tested. A technique that guesses is four-for-four.

## 3. What is left, in order

### 3a. Transposition table

**Measured and rejected at the first cost, before table code.** A 32-bit incremental key was
priced against the unchanged 16-bit build with `tests/c64search.c` under VICE. Both builds
visited exactly 152, 1,509 and 5,232 nodes. Their times were 289/1,660/10,460 and
336/2,005/11,963 jiffies respectively: 16.3%, 20.8% and 14.4% slower, or **15.3% combined**.
That clears the 10% stop in §5 by enough that the RAM design cannot rescue it. The memory and
per-target-policy questions were therefore not opened, and no transposition table was written.

The rest of this subsection records why it was the last candidate worth pricing and the costs
that would have followed if the first one had passed.

The TT was the only remaining technique the portfolio result did not already discount, because
it does not prune on an assumption: it returns the result of a search that actually finished,
so the depth it buys is real depth.

**The half that looked already paid for is not.** A hash *is* maintained for repetition
detection, at about 9% a node on a real C64 — but `geHashKey` is **16 bits**
(`src/engine.h:82`, `unsigned int`, and cc65's `int` is 16 bits). That is adequate for scanning
a short repetition ring, where a false match is rare and costs a wrongly-scored draw. **It is
not usable as a transposition key.** There are only 65,536 distinct keys; a 60,000-node search
collides constantly, and the index consumes most of the key, so almost nothing is left to verify
an entry with — a 1,024-entry table leaves 6 bits, roughly one accepted false hit in 64 probes.
A false hit returns a score and a move from an unrelated position, which is a blunder or an
apparently illegal move, not merely a weaker search.

**So the TT's first cost is widening the hash to 32 bits**, and that is paid in `eng_Make` and
`eng_Unmake` on *every* target, including the ones with no room for a table. Price it with
`tests/c64search.c` before writing any table code: if a 32-bit incremental hash costs enough on
a 6502, the TT is dead before it starts and that is worth knowing in an afternoon. The
repetition ring also doubles in size.

**Its second cost is RAM, and this is the question that needs Stefan.** At roughly 8 bytes an
entry — 32-bit key, score, depth, flag, move — the Atari's ~716 free bytes hold about 64
entries, which is useless, while the C64's ~11 KB holds a thousand or more, which is not. A
table sized per target means **seven targets playing different chess**, and every figure in
`doc/strength.md` is stated about "the engine". That is a portfolio decision, not a search
decision. §8 and the questions in the handoff.

**Other hazards:**

- **Determinism.** Every measurement in `doc/strength.md` depends on the search being
  deterministic, and a table carries state *between* searches: the same position can return a
  different answer depending on what was searched before it. One replayed game stays
  deterministic; `match sanity` and cross-configuration node equality may not. Decide what the
  invariant is *before* writing it, and keep the table switchable so the baseline is recoverable.
- **Bound handling** with fail-hard alpha-beta. This search returns `beta` on a cutoff and
  `alpha` otherwise, so exact/lower/upper bounds must be stored and used deliberately rather
  than copied from a fail-soft reference implementation.
- **Mate scores** must be stored relative to the ply or they are wrong when probed at another
  depth.
- **Level 1 will get nothing.** At 400 nodes there is almost nothing to transpose to. Expect a
  level 3–4 technique, which is the per-level gate case again.
- **Do not judge it on node savings.** §2 is why. It goes to the gauntlet.

### 3b. Node cost, and then the budgets

This is the lever with a *measured* exchange rate, and it is the one the portfolio's failure
points at.

**Phase 19 handed this section its first target for free.** Maintaining the 16-bit position hash
costs about 9% a node on a real C64, and widening that one field to 32 bits cost a further
15.3%. Hash maintenance is therefore the largest single item yet identified in the 6502 hot
path — and it exists only to serve repetition detection, which is load-bearing (§5.1 of
`doc/strength.md`) but may not need a full incremental hash to do its job. Roughly 9% of node
time sits behind that question, and unlike everything in §3 it is a *cost* reduction rather than
a pruning guess, so §2's exchange-rate objection does not apply to it. Start here.

At a fixed budget, cheaper nodes buy **time, not strength** — the same 400 nodes, sooner. The
strength comes from the second step: spend the time saving on a **larger budget**, at 60 Elo per
doubling, which is the rate that was measured by granting nodes and therefore the rate that
applies. Same move time, more real nodes, strength at a known price.

That is the trade Stefan offered when he said he would accept a small hit on move times, except
that done in this order it can cost nothing.

**Where to look:** `eng_Make`/`eng_Unmake`, move generation, and the incremental evaluation —
the 6502 hot path, priced with `tests/c64search.c` under VICE, never on the host. `AGENTS.md` is
emphatic that a cost measured on this host is not the cost: the position hash is 5.5% here and
9% on a real C64.

**Raising a budget is a deliberate, announced change**, not a side effect. It invalidates the
row of the ladder it touches and the on-target time in `README.md`. Do it with Stefan, not
around him.

### 3c. Very Easy, which is now a strength question and not only an ergonomic one

At ~1240 it is not a beginner's opponent, and 11 seconds a move on a stock C64 is slow for the
weakest setting. Lowering level 1's budget makes it **faster and easier at once**, both wanted.

It was out of scope while the search work was live. After §3b it is the same conversation, and
should be taken together with it.

**It needs design, not a smaller number.** Dropping the budget alone makes the engine
blunder-prone rather than gentle, and 400 nodes was already below the threshold for reliably
seeing mate in one before check evasions were added. An opponent that plays well and then hangs
its queen is more confusing to a beginner than a consistently modest one.

**What it costs to re-measure:** level 1's row of the ladder, `c64level1.c`, and the mate-in-one
suite. **Not** levels 2–4, and **not** any Sargon result — those were played at Easy and Harder.
A couple of hours on one rung, not everything.

### 3d. The opening, still open

The hope recorded in the previous revision — that the horizon problem behind cc65 arriving at
move 13 behind would be fixed by the search work — did not materialise, because none of the
search work landed. `doc/strength.md` §5.2b closed the evaluation explanation and the
statistic behind it. This remains unexplained, and the L4 rig below is now a better instrument
for looking at it than anything available when it was first noticed.

## 4. The instruments, and what they cost

The first instrument is the cheapest: compare candidate and baseline UCI builds over all 256
positions in `book.epd` at each of the four shipped budgets — total nodes, completed-depth sum,
positions deeper and shallower, move differences. It predicted PVS's level split before a match
or a target build, and stopped three candidates for pennies.

**A candidate needs roughly 20% node reduction at a level it targets before continuing** — but
read §2 on what that does and does not mean. It is a floor for measurability, not a promise.

```sh
cd tests && ./nodecompare.py --baseline ./uci-baseline --candidate ./uci-tuning
cd tests && make -B test                    # 35 s, must be green, never carry a red suite forward
./chesstest match sanity                    # 16 s, config against itself
./chesstest match <name>                    # 16 s, 512 games, A vs B self-play
./gauntlet.py --uci ./uci-tuning --levels 1,2,3,4 --nodes 1,30,100 \
              --games 512 --concurrency 6   # ~20 min, the statistic, all four levels
```

Sargon confirmation, ~4.3 hours for 64 games, following `sargon/README.md`'s boot choreography
— one harness, port 6511, never `--headless`:

```sh
python3 sargon/match.py --only match --match-games 64 \
  --cc-skill 3 --sargon-level 4 --move-timeout 300 \
  --output scratch/sargon-l4-<change>-YYYYMMDD
```

On-target node pricing, the only way to know what a change costs a real 6502 —
`doc/measuring.md` §6, headless, under warp, about a minute:

```sh
tests/c64search.c under VICE, shipping build twice, once with -DSEARCH_<X>=0
```

**512 games a pairing is the ceiling, not a floor.** Both engines are deterministic and node
limited, so `book.epd`'s 256 openings played twice is every distinct game that exists at a
pairing. Spend a bigger budget on more rungs, not more games.

**Three checks before any A/B is believed:**

1. **The switch off must reproduce the baseline exactly** — same moves, same node counts, same
   ladder numbers as §7.
2. **The switch on must change something.** A technique wired to nothing plays perfectly legal
   chess and looks exactly like a working one.
3. **`make -B`**, always, when patching and rebuilding in a loop. Sub-second mtimes have made
   this repo report green for a binary that was never rebuilt.

Every technique gets the **dual switch** that `SEARCH_CHECK_EVASION` in `src/search.h` models: a
`geSearchX` runtime flag under `EVAL_TUNING` for A/B matches out of one binary, and a `-D`
compile-time form so the shipping configuration can be built twice to price a node on target.
The tuning build cannot price a node, because tuning is what makes the node dearer.

## 5. The gates every change must clear

In this order. Stop at the first one it fails.

| # | Gate | Bar |
|---|---|---|
| 0 | Whole-book node/depth screen, all four levels | roughly **20% fewer nodes at a level the technique targets**; single-digit savings stop here as undetectable. A floor, not a prediction — see §2 |
| 1 | All seven targets build and play | not building or playing is a hard failure; **record** whether the Atari's `DLIST` crosses a page, but price that only at gate 8 |
| 2 | Test suite | green, and `match sanity` still level |
| 3 | Switch-off equivalence | reproduces §7 exactly |
| 4 | Self-play screen | a sign and a live-switch check; **not** a reason to land anything. It may support a *rejection* when a mechanism agrees — that is how PVS was closed |
| 5 | Stockfish gauntlet, all four levels | **pooled ≥ +2σ** to land; **any single level worse than −1σ** stops for a per-level decision |
| 6 | On-target node cost | measured and reported always; **over 10% dearer per node stops for a human decision** |
| 7 | Sargon L4, 64 games | no categorical regression — conversion, fifty-move draws, distinct games, a colour collapsing |
| 8 | Atari page price | if `DLIST` crossed, bring the 256-byte portfolio decision back **now**, with the strength evidence in hand |

Gate 5's arithmetic: twelve pairings pooled give a standard error of about 0.0066 in score, so
2σ is roughly 0.013, about 18 Elo. Compute it from W-L-D counts, not from a score alone; draws
make the variance smaller than `p(1-p)`.

Gate 7 is read for **categorical** outcomes, not the percentage — at 64 games the score's honest
interval is ±12 points. `doc/strength.md` §5.1.7.

## 6. Off-ramps

**Per item:** if it fails, revert it and write down what it measured. Do **not** try variant
after variant until one passes — that is fishing, and §5.2b is the worked alternative: when a
change measures as nothing, turn the mechanism *up* once to separate dose from mechanism, then
stop.

**Stop and report to Stefan if:** a target stops building or playing and the fix is not obvious
in one attempt; a change costs more than 10% per node on target; gate 3 fails and the cause is
not quickly found; or the Sargon rig shows a categorical regression the desk instruments missed.

An Atari `DLIST` crossing is a portfolio decision, not a stop, until gate 8. The cliff is a step
rather than a slope: any candidate can consume the ~62 bytes in front of it, and once one pays
the 256-byte step the next ~250 bytes fit without another jump. Establish that the portfolio has
something worth buying before asking it to pay.

**Success is any of:** a technique landed with its evidence; or a technique measured, rejected
and recorded. The second is a real result and this project has now published five. What is *not*
success is a landed change whose gates were skipped.

## 7. The baselines to diff against

`tests/uci-tuning` with default switches, 512 games a pairing, `book.epd`, both sides node
limited, Stockfish 18, fastchess. Matches `doc/strength.md` §4.1 within noise:

```text
level      SF n=1      SF n=30     SF n=100
  1         -285         -319        -429
  2         -180         -183        -246
  3          +44          +33         -51
  4         +150         +142         +16

mean score over the twelve pairings: 0.379
```

```text
chesstest match sanity : 196-196-120, conversion 81% of 422
Sargon L3, cc65 Harder : 42W 12L 10D = 73.4%, 27 distinct in 64, 1 fifty-move draw
Sargon L4, cc65 Harder : 22W 13L 29D = 57.0%, 33 distinct in 64, 0 fifty-move draws
```

**Level 4 is the calibrated outside pairing** — inside the 35%–65% band, 33 distinct games in
64, worst repeat 9×. Colour is 11W-9L-12D as White and 11W-4L-17D as Black (the old 15,000-node
Black 5-15-12 is gone). Sargon is not reproducible, so all comparisons are unpaired and both
sides need their own 64 games.

## 8. Scope

- **The transposition table is closed at its first cost**, so it no longer supplies a reason to
  reopen the visualizer split or choose a per-target RAM policy. No table code was written.
- **`gcSearchSkill` is in scope now, deliberately and with Stefan** — §3b and §3c. It was out of
  scope while search technique was live; that is over.
- **No changes to `plat.h`, the 0–63 tile numbering, the key bindings, or the ports.**

## 9. Authorisations

**The authorisations recorded in the previous revision were for the search-technique task, which
is finished. They do not carry forward.** In particular the pre-authorised unattended Sargon
runs were an explicit one-off exception to the standing preference to be asked before an
emulator takes the screen. **Ask again.**

The house rules that do carry: commit to a branch with the measurements in the message; never
push without being asked; and **write down what failed** — the most valuable sections of
`doc/strength.md` are the ones about measurements that demolished the plan around them, and
`doc/rework-log.md` is the unsanitised journal for exactly that.
