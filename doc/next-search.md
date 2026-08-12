# Next: the search, and how to know it worked

A work note for whoever picks this up next, human or agent. Written 2026-08-11 against
`c214407`. Read `AGENTS.md` first, then `doc/measuring.md` §3, then this.

It exists because the last round of work closed the project's most promising evaluation lead
(`doc/strength.md` §5.2b) and the evidence pointed somewhere else: **this engine is limited by
cycles, not by bytes, and its search has classical techniques missing that cost almost nothing
in either.** Nothing here is a certainty. Every item below is a candidate with a gate and an
off-ramp, and the note is written so that "measured, did not work, reverted" is a successful
outcome that takes half a day rather than a week.

---

## 1. What is already in the search

Do not re-derive this. `src/search.c` is 671 lines and already has:

- **iterative deepening**, with the previous iteration's best move tried first at the root;
- **killer moves**, two per ply, tried after captures;
- **MVV-LVA** capture ordering, promotions ranked above everything;
- **`pickBest`**, selection one move at a time rather than sorting the list — right, because a
  cutoff usually lands in the first few moves and the rest never get looked at;
- **quiescence** with stand-pat, and check evasions (which generate *all* moves, not captures);
- **repetition and fifty-move detection inside the search**;
- a **node budget** with a clean abort path, and a legal move banked before the first search so
  a budget overrun cannot look like stalemate;
- **randomised tie-breaking** for the opening, perturbing only moves already scored equal.

That ordering is good — `AGENTS.md` records about 2.28 moves tried per node before a cutoff —
and it matters, because the techniques below are the ones that pay off *when ordering is
already good*.

**Missing, in rough order of expected value:** principal variation search (every node currently
searches a full window), delta pruning in quiescence, skipping losing captures, null-move
pruning, history heuristic, SEE, check extensions, late move reductions, aspiration windows,
and a transposition table.

## 2. Why the search, and why the measurement is cleaner here

Strength in this engine **is** a node budget — the four menu levels are 400, 1,200, 15,000 and
60,000 nodes. So a search technique that reaches deeper within the same budget is a direct win
measured in the same unit the levels are defined in. That is a much cleaner story than the
evaluation ever had: every evaluation term that died here died on cost-per-node at equal time
(`doc/measuring.md` §3), and search efficiency does not have that problem.

**It follows that these changes should not cost move time.** Time per move on a real C64 is
budget × cost-per-node, the budget is unchanged, so the only genuine cost is the handful of
extra instructions the new code executes per node. That is expected to be a few percent and
**must be measured, not assumed** — see the gate in §5.

**Do not convert the gain into bigger budgets.** Raising `gcSearchSkill` is a separate decision
belonging to §8, and doing it silently would invalidate every published figure.

## 3. The work, in order

Do them one at a time, land or revert each before starting the next, and measure each **at all
four levels**. `AGENTS.md`: *a test that runs at one budget has not tested the skill levels*.
That trap is especially live here, because level 1 barely completes two plies on 400 nodes and
level 4 reaches five or six — techniques from the literature are tuned for engines searching
millions of nodes, and none of the expectations below should be trusted for this one.

Every technique gets the **dual switch** that `SEARCH_CHECK_EVASION` in `src/search.h` already
models, and the comment there explains why both halves are needed: a `geSearchX` runtime flag
under `EVAL_TUNING` so two configurations can play each other out of one binary, and a `-D`
compile-time form so the shipping configuration can be built twice to price a node on target.
The tuning build cannot price a node, because tuning is what makes the node dearer.

### 3a. Principal variation search

Search the first move at a node with a full window and every later move with a null window
(`alpha`, `alpha+1`), re-searching in full only when one fails high.

**Why first:** it is the smallest change here, it needs **no gate and no exception**, it cannot
misbehave in zugzwang, and it pays off more the better the move ordering already is.

**Hazards specific to this engine:**

- **16-bit arithmetic.** `int` is 16 bits on cc65 and holds ±32767; `EVAL_INFINITY` is 30000
  and `EVAL_MATE` is 29000, so there are only about 2,700 spare. `alpha+1` and its negation are
  safe at those values, but anything that *adds a margin* to a score near the mate range is not.
  Check every new expression by hand against a 16-bit `int` — `AGENTS.md` records a node guard
  that wrapped above 21845 and silently never fired, and no native test could reproduce it
  because the host has 32-bit ints.
- **Re-searches consume budget.** They are counted as nodes, which is correct, but it means a
  badly-ordered position can now spend more of its budget than before. Watch the weak levels.
- The root loop in `searchRoot` already searches every move with a full window. Deciding
  whether the root gets PVS too is part of the change; measure both.

**Expected:** a node-count reduction to a given depth in the low tens of percent, which at a
fixed budget shows up as extra depth. Unknown in Elo here. Treat anything positive as a win.

### 3b. Quiescence economies

Two separate changes, measured separately, not as a bundle:

1. **Delta pruning** — when standing pat plus the value of the captured piece plus a margin
   still cannot reach alpha, skip the capture.
2. **Skipping losing captures** — quiescence currently searches queen-takes-defended-pawn at
   full cost, because `scoreMoves` orders by MVV-LVA and orders nothing out. A cheap test
   (`eng_IsAttacked` on the target square, compared against the material swing) may be cheaper
   than searching the capture. Measure whether the test costs more than it saves; full SEE is
   almost certainly too expensive on a 6502 and is not what is being proposed.

**Why here:** at 400 nodes with a two-or-three ply search, quiescence is most of the tree, so
making it cheaper buys depth at every level and buys most at the weak ones — the same shape as
the check-evasion work, which took level 1 from 27 mates-in-one out of 60 to 55.

**Hazards:**

- **Both must be off when in check.** There is no stand-pat score in check, which is the whole
  point of the evasion work; a margin computed from a stand-pat that does not exist is nonsense.
- **Delta pruning misjudges the endgame**, where a pawn is a queen four moves later. Gate it on
  phase — `gePhase` already exists and `mateDrive` in `eval.c` is the precedent for using it —
  and read §5.1.6 before writing the gate, because that is the section where a gate passed
  every desk instrument and lost twenty points to Sargon.
- **The arena.** Quiescence bails to a static evaluation when `arenaRoom()` runs low, and
  `AGENTS.md` notes that running out of arena is a strength problem that produces no error of
  any kind. Anything that changes how deep quiescence goes changes arena pressure. Check it.

### 3c. Null-move pruning

Give the side to move a free pass and search the result to reduced depth; if it still fails
high, the position is good enough to prune.

**Why last:** it is the biggest single classical win and it is also, precisely, the risk profile
this project has been burned by. It needs a gate, and §5.1.6 is the story of a gate that every
internal instrument preferred and that lost the match against a real opponent.

**Hazards:**

- **Zugzwang.** These games run 120+ plies and spend real time in endgames, which is where the
  free pass is a lie. Gate on phase and on having pieces beyond pawns.
- **Not in check, not at the root, and not twice in a row.**
- **Making a null move is not free of side effects.** It must flip the side, clear en passant,
  and leave the position hash and `geHalfmove` in a state that `eng_IsRepetition` and the
  fifty-move test still read correctly. Getting this wrong produces a search that quietly scores
  positions as draws. Verify by checking that node counts and results are *identical* with the
  switch off.
- **R at shallow depth.** R=2 is standard for deep searches; at the three-to-five ply this
  engine reaches it may prune the search away entirely. Try R=1 as well and measure both.

**If it helps the deep levels and hurts Very Easy, gating it by node budget is permitted** —
see §9 — but the gate must be measured per level and confirmed against Sargon, not reasoned
about.

## 4. The instruments, and what they cost

The first instrument is now the cheapest one: compare the candidate and baseline UCI builds
over all 256 positions in `book.epd` at each of the four shipped budgets.  Report total nodes,
completed-depth sum, positions deeper and shallower, and move differences.  This screen
predicted PVS's level split before a match or target build: worse at levels 1 and 2, 5-6% fewer
nodes at levels 3 and 4.

**A candidate needs roughly a 20% node reduction at a level it targets before continuing.**
Twenty percent is about 0.32 doublings, or 19 Elo at the measured 60 Elo per doubling, against
the gauntlet's roughly 18 Elo two-sigma resolution.  A single-digit saving is below the noise
floor of the instrument that would have to approve it: stop before building targets or running
matches, and write the result down.  `tests/nodecompare.py` is the reusable screen.

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

On-target node pricing, which is the only way to know what a change costs a real 6502 —
`doc/measuring.md` §6, headless, runs under warp, about a minute:

```sh
tests/c64search.c under VICE, shipping build twice, once with -DSEARCH_<X>=0
```

**512 games a pairing is the ceiling, not a floor.** Both engines are deterministic and node
limited, so `book.epd`'s 256 openings played twice is every distinct game that exists at a
pairing; asking for more replays them and shrinks the interval on nothing. Spend a bigger
budget on more rungs.

**Three checks before any A/B is believed**, all of which have caught something here:

1. **The switch off must reproduce the baseline exactly** — same moves, same node counts, same
   ladder numbers as §7. If it does not, the switch is not the only thing that changed.
2. **The switch on must change something.** A technique wired to nothing plays perfectly legal
   chess and looks exactly like a working one. Diff the games.
3. **`make -B`**, always, when patching and rebuilding in a loop. Sub-second mtimes have made
   this repo report green for a binary that was never rebuilt, three times in a row.

## 5. The gates every change must clear

In this order. Stop at the first one it fails.

| # | Gate | Bar |
|---|---|---|
| 0 | Whole-book node/depth screen, all four levels | roughly **20% fewer nodes at a level the technique targets**; single-digit savings stop here as undetectable |
| 1 | All seven targets build and play | a target that does not build or play is a hard failure; **record** whether Atari's `DLIST` crosses a page, but price that only after the technique earns it |
| 2 | Test suite | green, and `match sanity` still level |
| 3 | Switch-off equivalence | reproduces §7 exactly |
| 4 | Self-play screen | a sign and a live switch; **not** a result, and not a reason to land anything |
| 5 | Stockfish gauntlet, all four levels | **pooled ≥ +2σ** to land; **any single level worse than −1σ** stops for a per-level decision |
| 6 | On-target node cost | measured and reported always; **over 10% dearer per node stops for a human decision** |
| 7 | Sargon L4, 64 games | no categorical regression — conversion, fifty-move draws, distinct games, a colour collapsing |
| 8 | Atari page price | if `DLIST` crossed, bring the 256-byte portfolio decision back only now, after the strength evidence exists |

Gate 5's arithmetic: the twelve pairings pooled gave a standard error of about 0.0066 in score
last time, so 2σ is roughly a 0.013 score difference, about 18 Elo. Compute it from the W-L-D
counts rather than from a score alone; draws make the variance smaller than `p(1-p)`.

Gate 7 is read for **categorical** outcomes, not for the percentage. At 64 games this rig now
yields about 27 distinct games and the score's honest interval is ±12 points. What carries a
result here is conversion, fifty-move draws, distinct games and whether one colour collapses —
`doc/strength.md` §5.1.7.

## 6. Off-ramps

**Per item:** if it fails gate 5, revert it and write down what it measured. Do **not** try
variant after variant until one passes — that is fishing, and §5.2b is the worked example of
the alternative: when a change measures as nothing, turn the mechanism *up* to find out whether
it is the dose or the mechanism, then stop. One dose test, then move on.

**Stop and report to Stefan, do not proceed, if:**

- any target stops building or playing and the fix is not obvious within one attempt;
- a change costs more than 10% per node on target;
- gate 3 fails and the cause is not found quickly, because everything downstream is then
  meaningless;
- the Sargon rig produces a categorical regression that the desk instruments missed — that is
  the interesting case and it deserves a human.

An Atari `DLIST` page crossing is still a portfolio decision, but it is not a stop until the
last gate.  The cliff is a step rather than a slope: essentially any remaining candidate can
consume the 62 bytes in front of it, and once one pays the 256-byte step the next roughly 250
bytes fit without another jump.  Measure whether the portfolio has something worth buying
before asking it to pay.

**Success for the whole exercise** is any of: one technique landed with the evidence written
down; or all three measured and rejected with the numbers recorded. The second is a real result
and this project has published several. What is *not* success is a landed change whose gates
were skipped.

## 7. The baseline to diff against

`tests/uci-tuning` with default switches, 512 games a pairing, `book.epd`, both sides node
limited, Stockfish 18, fastchess. Reproduced 2026-08-11 and matching `doc/strength.md` §4.1
within noise:

```text
level      SF n=1      SF n=30     SF n=100
  1         -302         -316        -415
  2         -182         -194        -259
  3          +19          +12         -74
  4         +160         +145         +16

mean score over the twelve pairings: 0.3699
```

```text
chesstest match sanity : 196-196-120, conversion 81% of 422
Sargon L3, cc65 Harder : 42W 12L 10D = 73.4%, 27 distinct in 64, 1 fifty-move draw
Sargon L4, cc65 Harder : 22W 27L 15D = 46.1%, 28 distinct in 64, 0 fifty-move draws
```

The L4 baseline is complete and inside the 35%-65% competitive band.  Its colour split is
17W-12L-3D as White and 5W-15L-12D as Black; that existing weak Black side is part of the
categorical baseline rather than a reason to average the colours together.  Sargon is not
reproducible, so all candidate comparisons are unpaired and both sides need their own 64 games.

## 8. Explicitly out of scope

- **Do not touch `gcSearchSkill`.** Making Very Easy genuinely easy and faster is wanted, and it
  is deliberately a separate job: it invalidates level 1's row of the ladder, `c64level1.c` and
  the mate-in-one suite (though *not* levels 2–4 and *not* any Sargon result, which were played
  at Easy and Harder). It also needs design rather than a smaller number — dropping the budget
  alone makes the engine blunder-prone rather than gentle, and 400 nodes was already below the
  threshold for reliably seeing mate in one before check evasions were added.
- **No transposition table, and no visualizer split.** The TT is the one item that genuinely
  needs RAM, which makes it the only thing that would justify a build without the `B`/`A`/`D`
  displays. Both are parked. Worth knowing when it is unparked: the position hash is already
  maintained for repetition detection at about 9% a node on a real C64, so a TT's marginal cost
  is the table and the probe, not the hashing.
- **No opening project yet.** The §5.2a lead is closed and §5.2b explains why the statistic
  behind it was fragile. What remains is a horizon problem, and 3a–3c are the fix for a horizon
  problem. Re-measure after they land and see whether it still exists.
- **No changes to `plat.h`, the 0–63 tile numbering, the key bindings, or the ports.**

## 9. Standing authorisations for this task

Agreed with Stefan on 2026-08-11, for this piece of work only:

- **The Sargon rig is pre-authorised and may be run unattended**, including 4.3-hour screens,
  without asking first. This is a deliberate exception to the standing preference to ask before
  launching an emulator, and it does not generalise past this task. All the runbook rules still
  hold: one harness, port 6511, never `--headless`, follow the boot choreography rather than
  improvising.
- **Commit accepted changes to a branch. Do not merge, do not push.** Put the measurements in
  the commit message; that is the house style and `git log` is where the evidence lives.
- **A technique may be gated by node budget** if it helps the deep levels and hurts Very Easy —
  measured per level, and the gate confirmed against Sargon rather than argued.

And the habit that matters more than any of the above: **write down what failed.** The most
valuable sections of `doc/strength.md` are the ones about measurements that demolished the plan
around them, and `doc/rework-log.md` is the unsanitised journal for exactly this.
