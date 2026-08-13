# Next: cheaper real nodes, then better chess

A work note for the next person or agent. It is deliberately a portfolio rather than one
implementation plan: the engine has no unmeasured technique left with a claim to be a large
win, so the work now is to price exact savings, combine the ones that survive, spend them on
real nodes, and separately test the few chess terms with evidence behind them.

Read, in this order:

1. `AGENTS.md`, including the target limits and traps.
2. `doc/engine.md` §5.3–§5.5 and §6.8–§6.11.
3. `doc/measuring.md` §3, §6 and §8.
4. `doc/next-search.md` §2–§6. Do not skip §2: null move saved 27% / 22% of its
   nodes and measured +0.04 sigma. Saved work and granted work are different currencies.
5. `doc/rework-log.md` Phases 5, 14–19, then 20–33 for A–E. Phase 5 is where the hot path
   was first measured; Phase 19 is why the transposition table is closed in the current
   architecture.

The short version of this note is:

> First make a real C64 do less exact work for the same search. Then deliberately grant the
> saved nodes back to the engine. In parallel, prefer chess knowledge that is free, incremental
> or confined to cheap positions. Nothing lands because it is plausible.

### Portfolio status (2026-08-12)

| phase | status | shipping effect |
|---|---|---|
| A profile | complete | retained `c64profile` instrument |
| B exact state | **B3+B4 kept** (−17.2% C64 fixed middlegame) | yes |
| C exact work | C1–C3 rejected; C4/C5 deferred | no survivors |
| D combine / buy nodes | **done** — L1/L2 bank time; L3 18k; L4 65k | L3 ladder up; L4 noise |
| E chess | E1/E4/E5/E3 rejected; E6 instrument; E2 null | no strength terms landed |
| F ordering screens | F1 rejected; F2–F5 open | no survivors yet |
| §10 Atari undo RAM | **kept** — ring at `$AF00–$B2FF` | Atari +1,024 BSS below `$9100` |

Shipping search is Phase B plus the Phase D budgets. Candidates retained default-off:
`EVAL_PAWNSTRUCT_ON`, `EVAL_KBN_ON`, `EVAL_DEV_ON`, `SEARCH_CHECK_EXT`,
`SEARCH_FOLLOW_PV`, plus the Phase C speed switches.

---

## 1. The ground truth this plan starts from

These are measurements, not estimates.

- The search tries about **2.28 moves per generating node** before a cutoff. Most generated
  moves are never made.
- The Phase 5 C64 profile was **41.9% generation, 18.7% make/unmake, 18.2% ordering,
  13.7% legality and 7.5% other** at budget 1,600, depth 3.
- That profile predates repetition detection, the second running evaluation, mate drive and
  check evasions. It is the best component profile available and is now old enough that the
  first action is to replace it, not to optimise its largest row by memory.
- Maintaining the current **16-bit** position hash costs about **9%** on a C64. The same
  change measured 5.5% on the host.
- The Phase 19 32-bit candidate visited exactly the same 152 / 1,509 / 5,232 nodes as the
  16-bit control and took 336 / 2,005 / 11,963 jiffies against 289 / 1,660 / 10,460:
  **15.3% slower combined**, before a TT probe or store.
- Check evasions cost **22.7%** where their fixed-game instrument reaches them and are
  load-bearing: level 1 mates in one went from 27/60 to 55/60.
- Mate drive costs **2.04%** in the endings where it runs and changed the known conversion
  failure. Its gates are the reason it is affordable.
- Granted search depth is worth about **60 Elo per doubling**. A perfect removal of a 9%
  tax is therefore only about eight Elo after the budget is raised to spend it. Exact small
  savings have to accumulate before the board visibly changes.
- The Apple II is the tight target for code: about **852 bytes** in MAIN and 2,025 in its
  low BSS. The Atari has about **1,387 bytes** to its framebuffer — the extra kilobyte is
  the undo ring at `$AF00–$B2FF`, not a smaller program — but only **4 bytes** before the
  next `DLIST` page jump. The other current figures are in `AGENTS.md`; measure maps again
  before buying anything.

One phrase needs care. Hash maintenance is the largest newly identified removable tax. It is
not larger than the old generation row. The current component profile has never separated
hash delta, history push, repetition scan, the three evaluation deltas, full generation and
capture generation on the engine that ships now.

---

## 2. Rules for this portfolio

### 2.1 A speed change plays identical chess

A change sold as speed must reproduce the baseline's moves, scores, completed depths and node
counts exactly. The native whole-book comparison is the cheap proof; a fixed-game C64 replay is
the target price. If a pure speed candidate changes one game, it is a chess change or a bug and
does not get banked under this heading.

Do not time two self-chosen games. Replay identical positions and require node counts to match
to the digit before reading the clock. Do not price from the opening: the middlegame is about
25% slower per node.

### 2.2 Cheaper fixed budgets are not stronger

At the shipped budget a faster node returns the same move sooner. Strength changes only when
the saving is deliberately converted into a larger `gcSearchSkill` budget. Raising a budget:

- is a separate commit and measurement;
- is uniform across all targets;
- invalidates that level's ladder row and its time in `README.md`;
- needs Stefan's decision, including whether to spend all of the saving or keep faster moves.

Use `60 * log2(newBudget / oldBudget)` only for granted nodes. Do not use it to predict what a
pruning technique is worth.

### 2.3 A chess change needs an outside opponent

Use the switches and gates in `doc/next-search.md`. Self-play is a live-switch and mechanism
check, not landing evidence. The Stockfish gauntlet decides; Sargon confirms changes that pass.
The pooled bar remains +2 sigma, with any level below -1 sigma stopping for a per-level decision.

For a targeted defect such as KBN mate, its purpose-built conversion suite is a co-equal gate.
A rare ending need not move global Elo to be worth fixing, but it must fix the named failure
without causing an outside regression.

### 2.4 One mechanism, one candidate, one dose

Do not keep changing a neutral feature until one version wins. If a well-founded candidate
measures as nothing, turn its mechanism up once to distinguish an insufficient dose from an
inert mechanism, then stop and record it.

### 2.5 Every shipped target plays the same chess

Common budgets, common table sizes, common clearing rules and common search state. A target may
use a different implementation only when it is proven behaviorally identical—for example a
ca65 hot routine with the C routine retained for `term`. Per-target TT sizes, evaluation terms
or budgets are out unless Stefan explicitly changes the portfolio rule.

No `plat.h`, tile-number, key-binding or speculative platform edits. Do not fund chess by
degrading `B` / `A` / `D`.

### 2.6 Authorisations do not carry between tasks

Ask before launching VICE, a2m, Altirra, Sargon or another emulator unless the current task
explicitly authorises it. An earlier unattended run was permission for that run, not this
portfolio. Work on a branch; never merge or push unless Stefan asks.

---

## 3. Phase A: replace the stale C64 profile

This is the first action. Keep the instrument this time. Use the doubling method from
`doc/measuring.md` §6 and a fixed middlegame replay. Split at least:

| Component | What to double or isolate |
|---|---|
| full move generation | a second generation into static/shared scratch space |
| capture/promotion generation | the same in quiescence positions |
| scoring | a second idempotent `scoreMoves` |
| selection | a second `pickBest` after the element is already placed |
| ordinary legality | a discarded second `eng_IsAttacked` |
| board make/unmake | an exactly self-reversing extra pair |
| middlegame score delta | duplicate only `eval_MoveDelta` |
| endgame delta | duplicate only `eval_EndDelta` |
| phase delta | duplicate only `eval_PhaseDelta` |
| hash delta | duplicate the piece-placement update only |
| history construction | duplicate `positionKey` and ring write without changing state |
| repetition scan | repeat the scan and discard its answer |

Every row must walk the same nodes as baseline. Use enough work for jiffy resolution and run
the pair interleaved more than once. Record CODE, RODATA, DATA and BSS along with cycles; an
Atari page can cost more than the code that triggered it.

**Phase 20 result.**  The retained profiler now does this over six fixed middlegame positions,
twice with baseline/candidate order reversed.  Exact C64 shares were: capture generation
29.27%, full generation 9.28%, middlegame delta 8.98%, hash delta 8.49%, endgame delta 8.04%,
scoring 7.23%, legality 6.15%, selection 5.02%, board-only make/unmake 4.71%, phase delta 1.98%,
history construction 0.56% and repetition scan 0.16%.  All rows walked the same 14,400 nodes
and returned the same result digest.  Phase A is complete; the B and C order should use these
numbers rather than the Phase 5 table.

The result chooses the order below. The order written here is the prior, not permission to skip
the measurement.

---

## 4. Phase B: remove unnecessary hash and unmake work

The Phase 19 rejection remains the result for the current architecture. This phase is not a TT
implementation. Repetition detection is load-bearing and must remain exact relative to the
existing 16-bit collision policy.

### B1. Use the ring's current key in `eng_IsRepetition`

`eng_Make` has just written the current `positionKey()` into the newest ring slot.
`eng_IsRepetition()` reconstructs it again from `geHashKey`, castling and en passant. Try reading
the newest ring entry instead.

Proof obligations:

- new game, FEN, actual move, search move, undo and redo all leave the newest entry equal to
  `positionKey()`;
- `tests/repetition.c` and the fuzzer stay green;
- whole-book output is identical;
- target timing is measurable before it stays.

This is small enough that a no-result is expected and acceptable.

**Phase 21 B1 result: rejected.**  The invariant holds across reset, FEN, actual and search
moves, undo and redo, and all 1,024 whole-book searches were identical.  On the fixed C64
middlegame, doubling the old reconstruction path cost 69 jiffies and doubling the newest-ring
path cost 55, against 46,000-jiffy baselines.  The shortcut therefore saves about 14 jiffies,
or **0.03%** of the replay, while the baseline pair itself differed by nine.  The candidate and
its invariant check remain behind `ENGINE_REPETITION_RING_KEY` for reproduction, default off;
the shipping path is unchanged.

### B2. Do not maintain history for a temporary legality probe

The search makes a pseudo-legal move, updates the hash and ring, asks whether its king is
attacked, then immediately unmakes illegal moves. No consumer reads the temporary key.

Separate board/evaluation mutation from history mutation so a candidate is hashed and pushed
only after it is known legal and immediately before recursive `negamax`. Preserve the existing
full `eng_Make` behavior for actual game moves and callers that rely on it.

This is an API refactor with a silent-failure mode. Add a fuzzer assertion that the board,
evaluation, phase, key, ring top and valid count are identical after every rejected probe.

**Phase 22 B2 result: rejected.**  A split probe/commit API passed the state fuzzer and all
1,024 whole-book searches, but the fixed C64 replay was 47,350 jiffies against 47,324 for the
same refactored code using the old full probes.  Both were slower than the 46,450-jiffy Phase A
engine because cc65's extra C calls cost more than the illegal probes saved.  Enabling B2 beside
B3 changed its final 42,300-jiffy replay by less than 0.1% and reversed direction across pairs.
The shipped `eng_Make` interface and ordinary legality path therefore remain unchanged.

### B3. Do not maintain history in quiescence

`quiesce` never calls `eng_IsRepetition`. Check evasions mean it can now contain quiet moves,
so the old comment that it searches captures and promotions only is no longer a sufficient
argument. The actual invariant is narrower: no code in that subtree reads the key or ring, and
the parent state is restored exactly on return.

Give quiescence a no-history make/unmake path. Prove:

- identical moves, scores, node counts and completed depths over all 1,024 whole-book searches;
- identical mate-in-one, quiescence and game-fuzz results;
- parent key and ring state survive abort, arena exhaustion, mate and every ordinary return;
- a fixed C64 middlegame replay improves enough to keep the added code.

This is likely the largest hash experiment because quiescence owns most generating nodes. It
must be measured rather than inferred from that sentence.

**Phase 22 B3 result: kept.**  History maintenance is disabled once at the quiescence boundary
and restored on every return, while board and all three evaluation totals continue to use the
ordinary make/unmake path.  All 1,024 whole-book records were identical.  Explicit tests cover
ordinary, budget-abort, arena-exhaustion and mate returns and compare the parent board, totals,
key, entire ring digest, EP, castling, halfmove and king state.  The final fixed C64 replay fell
from 46,558 to **42,234 jiffies, 9.3%**, with identical 14,400 nodes and result digest.
The compact boundary-mode implementation costs 46 bytes: Atari `DATA` ends `$7BEF`, leaving 16
bytes before `DLIST` without taking its page jump and retaining 716 bytes below the framebuffer;
Apple II MAIN ends `$B29F`, leaving 1,120 bytes, and BSS is unchanged.

### B4. Restore running state instead of recomputing it on unmake

A quiet make/unmake calculates the same two-piece hash delta twice. It also calls the same
middlegame, endgame and phase delta functions twice. `SEARCH_MAX_PLY` is twelve, so a search-only
stack holding the old hash, `geEvalScore`, `geEvalEnd` and `gePhase` costs 8 bytes per ply,
96 bytes total.

Do not enlarge the 128-entry user undo ring by eight bytes an entry. Keep its slow path or give
search a separate extended state. The target is for search unmake to restore four totals while
still restoring board, king, en passant, castling and halfmove state normally.

The fuzzer already recounts evaluation, phase and hash. Extend it to compare the fast and slow
unmake paths over the same moves, including promotion, en passant and both castles.

**Phase 23 B4 result: kept.**  Search stores the old piece hash, middlegame total, endgame total
and phase in one 8-byte state per reachable ply, 96 bytes total.  Unmake still restores board,
king, EP, castling, halfmove and history top normally, then search restores the four totals.
The fuzzer compares fast and slow unmake on every chosen move, with special moves preferred;
all 1,024 whole-book records are identical.  On top of B3 the fixed C64 replay fell from 42,282
to **38,557 jiffies, 8.8%**.  Together B3+B4 are 17.2% below the exact 46,558-jiffy control.
Four parallel state arrays were 0.8 percentage points faster but cost a second Atari display-list
page; the compact struct keeps `DLIST` at `$7D00`.  Atari now has 363 bytes below the framebuffer;
Apple II has 856 bytes in MAIN and 2,025 in BSS.

### B5. Do not call a delta helper whose answer is known to be zero

`eval_PhaseDelta` is nonzero only for a non-pawn capture or promotion. `eval_EndDelta` can be
nonzero only when a pawn or king moves, promotes, or one is captured. They are currently called
for every move in both directions. If the new profile assigns them real time, guard the calls
from the piece bytes and flags already in `eng_Make`, or fuse the three evaluation updates so
their common move decoding happens once.

The branches themselves cost cycles, so price quiet, capture-heavy and endgame replays. Preserve
the pure-delta reference implementation for fuzz comparison. Do not fold a board read into a
delta function; make and unmake see different boards.

**Phase 24 B5 result: rejected.**  Direct guards were exact across the suite and 1,024
whole-book searches and reduced the fixed C64 replay from 38,557 to 37,662 jiffies, **2.3%**.
They added 71 bytes, however, moving Atari `DLIST` from `$7D00` to `$7E00` and leaving only 107
bytes below the framebuffer.  A single fused update call saved two bytes but only 0.7% because
its extra cc65 call erased most of the guarded work.  Neither form justifies a 256-byte Atari
page.  Both were reverted; the pure delta path remains.

### B6. Only then hand-code the remaining key update

The current cc65 output for `pieceKey` performs software-stack traffic and calls 16-bit shift
helpers before its two-byte load. A common ca65 implementation has room to win.

Prefer a separate assembly module to inline assembly inside C:

- use only instructions common to the shipped 6502-family targets;
- retain the C implementation for `term` and as the reference;
- exploit the table's 128-byte colour blocks and 256-byte piece-kind pairs;
- consider a 24-byte block-pointer table before page alignment, since alignment padding can
  push Atari over a `DLIST` cliff;
- compare C and assembly over perft, fuzz, all whole-book records and target node counts.

Perfectly removing the whole current hash tax is only about eight Elo after reinvestment. Keep
assembly only for a measured target win, not because the generated listing looks offensive.

**Phase 25 B6 result: rejected at the required profile screen.**  B3 removes history from
quiescence and B4 no longer recomputes the key on search unmake, so the old 8.49% Phase 20 row
is no longer the price of the remaining operation.  On the retained fixed C64 replay, doubling
`hashDelta` changed 38,557 baseline jiffies to 38,724, with the same 14,400 nodes and result
digest.  Even deleting the operation entirely could therefore save only **0.43%**.  B4 also
leaves eight bytes before Atari's next `DLIST` page.  A common assembly module cannot justify
that ceiling or the page risk, so no assembly implementation was started and the C reference
remains the shipping path.

### B7. A different 16-bit repetition signature is allowed as a new experiment

Zobrist randomness may not require 1,536 bytes and a generic piece-square lookup when the only
consumer is a short repetition ring. A smaller square table plus rotations, or two cheap
independent byte accumulators, could be faster and smaller.

It must first be evaluated offline over millions of real and fuzzed positions for distribution
and false matches. A collision returns a draw score in a possibly won position, so a clever
checksum with visible structure is not an acceptable substitute for evidence.

**Phase 25 B7 result: rejected before changing the collision policy.**  The companion C64
screen doubled all remaining history construction, including `positionKey` and the ring write:
38,556 baseline jiffies became 38,584, again at identical nodes and digest.  That row is
**0.07%**.  Together the entire remaining piece-key and history path is only about **0.51%** of
the replay before any replacement lookup, rotation or accumulator is added.  A smaller
signature could recover much of the 1,536-byte Zobrist table, but the current Atari build has
363 bytes of real headroom and no accepted Phase B feature needs that table space.  That size
opportunity alone is not a reason to replace a measured random signature with a collision-risk
experiment.  No candidate crossed the performance prerequisite, so the million-position
collision campaign was deliberately not opened.

**Phase B is complete.**  B3 and B4 are the two survivors and together reduce the exact fixed
C64 replay from 46,558 to 38,557 jiffies, **17.2%**, without changing any of the 1,024
whole-book records.  B1, B2, B5, B6 and B7 are closed on their recorded measurements.  The
forced native suite and all seven `optsize` target builds are green.  Final tight-target maps
put Atari `DLIST` at `$7D00` with 363 bytes below the framebuffer and leave Apple II 856 bytes
in MAIN plus 2,025 in BSS.  The shipping state is on
`codex/next-engine-phase-b-exact-state`; Phase C has not been started.

---

## 5. Phase C: exact work outside the hash

Run these as separate candidates. Their ceilings come from the new profile, not Phase 5.

### C1. Score and select the first move in one pass

`scoreMoves` visits every move; the first `pickBest` immediately scans them again. Have the
scoring pass remember and place the best move, and begin the search loop with element zero
already selected. Later selections remain unchanged.

Root random perturbation and previous-iteration priority happen after scoring and need their
existing path. The internal negamax and quiescence lists are the simple first target. Require
the ordered move sequence itself—not only the final move—to match baseline on a native test.

**Phase 26 C1 result: rejected.**  The fused internal path is exact when root keeps plain
scoring: placing the best before root randomisation or previous-iteration priority rewrites
scores changes later tie order among equals and is not a pure speed change.  With root left
alone, all 1,024 whole-book records match and a native selection-order test agrees for full and
capture lists.

The size-conscious single-function form (`placeFirst` flag, one score loop) grows Atari CODE by
210 bytes, moves `DLIST` from `$7D00` to `$7E00`, and on the fixed C64 middlegame is slightly
*slower*: 38,557 -> **38,589** jiffies at the same 14,400 nodes.  A duplicated
`scoreMovesSelectFirst` body avoids the flag tax and measured 38,557 -> 38,418 (**0.36%**) but
overflows Atari by hundreds of bytes.  Neither form justifies the display-list page.  The
candidate remains behind `SEARCH_SCORE_FIRST`, default off; shipping is unchanged.

### C2. Test legality on demand, not by building a pin set

The pin set was correctly rejected: it computed eight rays at every generating node to avoid
tests on only 2.28 tried moves. The per-move version has a different cost shape.

When the side was not already in check, an ordinary non-king move can expose its king only if
the origin is aligned with the king. The exact fast path is:

- king move: full attack test;
- en passant: full attack test—the removed victim is on another square;
- side already in check: full attack test;
- origin not on the king's rank, file or diagonal: legal without a full attack test;
- aligned origin: after make, scan only the vacated ray for a compatible enemy slider.

Castling retains its existing crossed-square tests. Add constructed tests for horizontal
en-passant discovery, every slider direction, an aligned but unpinned piece, capturing the
would-be attacker, double check and a move that interposes while already in check. Then fuzz.

This is exact but high correctness risk. Stop if the speed is single-digit noise or the first
implementation cannot be made obviously correct.

**Phase 27 C2 result: rejected for size.**  `eng_LeavesInCheck` is exact: all 1,024 whole-book
records match the full `eng_IsAttacked` path, depth-five perft is unchanged, and constructed
cases cover horizontal en passant discovery, orthogonal and diagonal pins, aligned-but-free
pieces, capturing the pinner, interpositions and double check.  The native suite forces
`ENGINE_FAST_LEGAL=1` so those gates stay live.

Two forms were sized on Atari `optsize`.  A dedicated vacated-ray scan grew engine CODE by
about 694 bytes and overflowed MAIN by 661.  The size-conscious form only early-outs when the
origin is unaligned and still full-tests aligned moves: engine CODE +293 bytes, still
**149 bytes over** the Atari MAIN ceiling.  On the fixed C64 middlegame that slim form is
38,557 -> **37,665** jiffies (**2.3%**) at the same 14,400 nodes — the same percentage B5
had, and the same verdict: not worth an Atari page or a hard overflow.  `ENGINE_FAST_LEGAL`
defaults off; shipping maps again match Phase B (`DLIST` `$7D00`, DATA ends `$7CF7`).

### C3. Give quiescence a dedicated capture/promotion generator

The shared generator tests `sc_capturesOnly` through pawn, stepper and slider loops and calls a
generic five-argument `addMove`. A dedicated generator can omit quiet-square writes and use a
current output pointer with one capacity check.

The result must be the exact subsequence of `eng_GenMoves`, in the exact same order; existing
quiescence tests enforce this because tie order changes chess. First write clear C. If capture
generation remains hot, price a common ca65 slider/stepper kernel with the C implementation as
reference.

**Phase 28 C3 result: rejected for size.**  A clear-C dedicated capture generator (write
pointer, no `sc_capturesOnly` branches, no quiet emits) matches the filtered full generator on
the existing quiescence walk and keeps all 1,024 whole-book records.  On the fixed C64
middlegame it is a real win: 38,557 -> **36,145** jiffies (**6.3%**) at the same 14,400 nodes.
It also grows Atari engine CODE by about **1,088 bytes** and overflows MAIN by 921 — far past
the 363-byte framebuffer headroom and the eight-byte `DLIST` pad.  No ca65 kernel was started:
the C form is already too large, and assembly would not shrink the duplicated pawn/stepper/
slider walk enough to link.  `ENGINE_DEDICATED_CAPTURES` defaults off; the native suite forces
it on so the order gate stays live.

### C4. Consider zero page only after all target maps are read

Hot scalars and generator pointers may benefit from zero-page placement. Availability and
runtime reservations differ by target. Do not assume a C64 map describes Apple II, Atari,
Atmos, Plus/4 or CX16. A zero-page candidate must build all seven 8-bit targets and preserve
the terminal build before it is timed.

**Phase 29 C4 result: deferred — no common free zero page.**  Shipping maps use the full
cc65 `ZEROPAGE` reservation of `$1A` bytes on every target measured:

| target | ZP start | used |
|---|---|---|
| c64 / c64.chr / plus4 | `$02` | `$1A` (full) |
| cx16 | `$22` | `$1A` (full) |
| atmos | `$E2` | `$1A` (full) |
| atari | `$82` | `$1A` of `$7E` configured |

Only Atari has headroom beyond the runtime block.  Putting hot engine state there would be an
Atari-only layout and violate the same-chess / common-structure rule.  Expanding ZP into
platform-reserved bytes is not safe without per-target proof the OS and cc65 stack leave them
alone.  No zero-page candidate was coded.

### C5. Piece lists remain low priority

The 120-square scan was only 7% in Phase 5. A piece list adds work to every make/unmake and a
new invariant to every special move. Reconsider only if the new profile says the scan changed
materially; do not justify it with perft.

**Phase 29 C5 result: still deferred.**  Phase 20 folds the board walk into full generation
(9.28%) and capture generation (29.27%); it does not show a separate empty-square scan tax
large enough to pay for per-side piece lists, make/unmake updates and special-move invariants.
C3 already priced a faster capture generator at 6.3% and could not fund it on Atari.  Piece
lists remain lower priority than that still-blocked path and were not implemented.

---

## 6. Phase D: combine exact savings and buy nodes

Do not combine candidates until each has its own target number and switch-off equivalence. Then
measure the combination: savings need not add when two changes remove overlapping work.

For each surviving speed change record:

```text
baseline and candidate commit
fixed replay and target
nodes, jiffies and nodes/sec
CODE / RODATA / DATA / BSS delta
Atari DATA end, DLIST start and framebuffer headroom
Apple II MAIN and BSS headroom
whole-book equivalence digest
```

Bring the measured aggregate to Stefan with two explicit choices:

1. Keep some or all of the faster move time.
2. Raise one or more node budgets by the measured factor and re-run only the affected ladder
   rows before the full gauntlet.

A deterministic game-stage multiplier is also eligible. Endgame nodes are cheap, so a larger
endgame budget may spend otherwise unused wall time on conversion while every target still
plays the same chess. It needs fixed-game C64 timings by phase and separate authorization: it
is a budget change, not a free consequence of optimization.

Very Easy is its own design. Lowering 400 alone previously lost reliable conversion. A beginner
opponent should be consistently modest rather than sound until it hangs a queen. Any redesign
must retain a tactical safety floor, run mate-in-one at its actual budget, and update
`tests/c64level1.c` and its ladder row.

**Phase 34 D result.**  B3+B4 were the only speed survivors. The spend:

| level | nodes | what happened |
|---|---|---|
| Very Easy | 400 (unchanged) | same chess; C64 20-ply mean **11.0s** (was 13s) |
| Easy | 1,200 (unchanged) | same chess; C64 20-ply mean **39.7s** (was 46s) |
| Harder | 15,000 → **18,000** | 15/256 whole-book positions complete another ply, 5 change move |
| Very Hard | 60,000 → **65,000** | 23/256 complete another ply, 5 change move |

18,000 is not cosmetic. Depth caps unchanged. Mate-in-one unchanged (10/12/12/12). Convert
13/13 at L3 mean 22 plies (was 23) and L4 mean 21 (was 19).

Harder ladder, 512 games, **same-day** 15,000-node control (not the historical Appendix A):

| vs SF | 15,000 today | 18,000 |
|---|---|---|
| 1 node | +19 | **+44** |
| 30 | +12 | **+33** |
| 100 | −74 | −51 |
| 300 | −217 | −205 |

About +20 Elo at the near-50% rungs. Very Hard 65,000 vs same-day 60,000 is noise (−10 / −3 / 0 / −8).

Sargon L4, 64 games: **22-13-29 (57.0%)**, 33 openings, 0 fifty-move. Previous 15k pairing
was 22-27-15 (46.1%), 28 openings, weak Black 5-15-12. Black is now 11-4-17. Confirmation,
not a regression.

---

## 7. Phase E: direct chess candidates with a reason to work

These do not wait for every speed item, but each is a separate experiment. Prefer terms with a
prior measurement, zero node cost, or a gate that moves execution out of the expensive game.

### E1. Rebuild pawn structure incrementally

The old doubled / isolated / passed-pawn bundle scored **+2.0 sigma at equal nodes** and died
because a full-board leaf evaluation made every C64 node **1.35x dearer**; at equal time it was
+0.6 sigma and cost 735 bytes. The positive signal is real and the implementation price is no
longer compulsory now that evaluation is incremental.

Start with per-side pawn counts by file and an aggregate score updated on pawn moves, captures,
en passant and promotion. Doubled and isolated pawns affect only the old/new files and their
neighbours. Passed pawns have wider dependencies and should be a second, separately switched
step if they cannot be updated locally and cheaply.

Required checks:

- a full recount agrees after every fuzz move, undo and redo;
- the switch-off tuning build reproduces baseline exactly;
- target node cost is measured before equal-time self-play;
- it passes the complete Stockfish gauntlet and Sargon if ranked;
- all target maps are recorded—file counts consume BSS on the machines that have least.

This is the highest-priority direct strength term because it has prior evidence and a known
removable reason for rejection.

**Phase 30 E1 result: rejected for equal-time cost.**  Incremental make/unmake file counts
were exact but cost ~750 CODE bytes and overflowed Atari.  A pawn-only board scan at eval
time fitted (`pawnstruct.s`, Atari 82 free when on) with doses −8/−16.  Equal nodes 228-208-76
over 512 (~+0.9σ); host nps ~1.38x dearer; equal time 182-244-86 (~−2.5σ).  Switch-off matches
Phase C on all 1,024 whole-book searches.  `EVAL_PAWNSTRUCT_ON` defaults 0; not in `EVAL_ALL`.
Reopen only with a free or near-free update (true incremental that fits, or a memory reclaim
that funds it) — not another full-board walk.

### E2. Tune existing values at zero runtime cost

Piece values, the existing PST cells, endgame tables, phase thresholds and blend weights can be
changed without adding a lookup or byte if their shapes stay fixed. The failed queen-home test
closed one coordinate and one mechanism, not the table family.

Use positions the engine actually reaches. Split by complete games or opening pairs into
training and locked validation sets so positions from one deterministic game cannot leak into
both. Fit a constrained, symmetric and smooth candidate; freeze it; then run the ordinary
gauntlet once. Do not tune against the landing set.

**Phase 35 E2 result: existing numbers already sit at the constrained minimum.**  Positions
the shipping engine actually reached, self-play from `book.epd`, even openings train / odd
locked val, pawn = 100, one scale per existing table, no singleton d1.  45,878 / 44,740
positions.  The fit moved bishop 330 → 327; the freeze is bishop 325 and every scale 1.00.
Val mse 0.127940 → 0.127896.  Near-shipping; a null is done, no gauntlet of a no-op.
E6's queen-move count was not an input.

### E3. Test an opening interaction, not another queen square

The remaining qualitative opening residue is temporal: the evaluation cannot say "queen out
before the minor pieces," because a PST only knows the queen's square. A position-based
interaction can: queen off home while original bishops/knights remain home, optionally with
castling state.

Maintain it incrementally from the few home squares so it is paid only when one changes. This
is a new mechanism and needs a zero/small/large predeclared dose, the full outside gauntlet and
Sargon confirmation. Self-play cannot decide an opening weakness shared by both sides.

**Phase 36 E3 result: rejected.**  Doses 0 / −16 / −48 were declared first. Incremental
make/unmake overflowed Atari; the eval-time 10-square scan links and takes a `DLIST` page
(363 → 105). Switch-off is exact (1,024 searches). Dose 16 vs same-day control:
unweighted **+1.95σ**, pooled +1.67σ, every level non-negative — short of +2σ. Dose 48
is worse (−0.43σ, L3 −1.33σ). Mechanism live, not enough, stronger dose fails. Defaults
off; not in `EVAL_ALL`.

### E4. Fix KBN versus king as a named defect

King, bishop and knight against a bare king is 0/25. Add a material- and phase-gated term that
knows the bishop's colour, drives the losing king toward a matching corner, brings the winning
king close and gives the knight a usable gradient. It should execute nowhere else.

Judge it first on conversion before the fifty-move rule at all four real budgets, then against
a fixed outside defender. As with mate drive, global Elo may be insensitive to a rare ending;
an outside regression is still disqualifying.

**Phase 31 E4 result: rejected for Atari size (and incomplete lower levels).**  A
colour-aware corner drive gated on `gePhase == 650` converts **level 4** positions when given
~25k+ nodes (host), but the C form that works is ~700–1200 CODE bytes over Atari's budget.
A size-slim form (corners + king only) still overflows and only reaches ~4/8 at level 4.
Levels 1–3 stay 0/8 at their real budgets either way.  `EVAL_KBN_ON` defaults 0; not in
`EVAL_ALL`.  Reopen with a ca65 kernel under ~300 bytes or a memory reclaim that funds the C.

### E5. Try one bounded check extension

Sargon extends checks even at its weakest setting; this engine has check evasions in
quiescence but no main-search extension. Test one extra ply when the side to move is in check,
bounded by `SEARCH_MAX_PLY`, the arena and the node budget. Consecutive checks must not escape
those limits.

An extension spends nodes, so the 20% saving pre-gate does not apply. Screen move/depth changes,
mate and defensive-only-move suites at each actual skill budget, then use the gauntlet. Stop if
one level falls below -1 sigma.

**Phase 32 E5 result: rejected.**  One-ply check extension (`SEARCH_CHECK_EXT`) is exact
relative to the ply/budget caps but at fixed node budgets it **spends** nodes: whole-book L1
and L3 complete less depth and change moves; mate-in-one at level 1 fell from 11/12 to 10/12
(below the floor).  Atari pays a display-list page (free 363 → 107).  Defaults off.

### E6. Mine failures before inventing more evaluation

For new terms, collect the first durable Stockfish swing from Sargon and gauntlet losses, remove
mate-clamped scores, and group positions by mechanism. A candidate needs repeated positions
showing the same defect, not an average over piece types or one attractive game. Bishop and
queen opening moves already demonstrated how a plausible aggregate can measure disagreement
without identifying an error.

Potential later gated terms include bishop pair, castling/development interaction, rule of the
square and KPK opposition. Pawn-shield king safety is closed at -2.6 sigma; any new king-safety
term must measure enemy pressure rather than reintroduce that proxy.

**Phase 33 E6 result: instrument shipped; sample mining recorded.**  `tests/mine_failures.py`
walks a PGN of our losses, takes Stockfish's first durable non-mate swing after our move
(default −150 cp, depth 12), and counts piece-kind tags.  Sample: L3 vs SF-n30, 64 games,
24 losses, 23 swings — **queen-move 7, pawn 5, bishop 4, knight 4, rook 2, king 1** (median
swing −203 cp).  That is a reason to prefer queen-related E2/E3 work over inventing a new
king-safety term; it is not yet a landing candidate.  No new evaluation term added.

---

## 8. Phase F: exact ordering ideas still worth cheap screens

These can change the move reached under a fixed node budget, but they do not discard a completed
search on an assumption. Use the whole-book screen before any target work.

### F1. Carry the previous full PV

**Phase 38 F1 result: rejected.**  Previous-iteration full PV, tried first while the new
search stays on that line. Dual switch `SEARCH_FOLLOW_PV` / `geSearchFollowPV`, default off.
Switch-off is exact (1,024 searches). Whole-book with the switch on:

| level | nodes | saving | deeper / shallower | moves |
|---|---:|---:|---:|---:|
| 1 | 63,224 / 63,224 | 0.00% | 0 / 0 | 0 |
| 2 | 290,234 / 290,234 | 0.00% | 0 / 0 | 0 |
| 3 | 2,983,451 / 2,995,804 | **−0.41%** | 2 / 0 | 1 |
| 4 | 14,912,657 / 14,709,118 | +1.36% | 10 / 1 | 2 |

Single-digit, one level spends more, a handful of move changes. Not a speed candidate
and not a chess change worth a gauntlet. The previous PV itself is 48 bytes; the
triangular that collects it is 576 plus 12 length bytes. Compiling the candidate on
grows CODE by 1,012 bytes and BSS by 638, overflows Atari MAIN by 275 and Apple II
MAIN by 160, and walks `DLIST` `$7D00` → `$8100`. Shipping (default off) maps are
unchanged: Atari `DATA` `$7CFB` / `DLIST` `$7D00`, Apple II MAIN 852 free. One
candidate, one screen; stop.

### F2. Preserve all previous root scores

The previous winner gets score 255 and every other root move falls back to ordinary ordering.
Retaining the previous iteration's root order can improve the windows of later root children.
Keep the representation compact and deterministic.

### F3. Compact history heuristic

A 64×64 from-to table is out of scale. A saturating piece-to-destination table is 6×64 bytes
and may be enough. Clear it at every `search_Best` so prior games cannot affect determinism.
Current ordering is already near-minimal; expect it to fail cheaply unless the whole-book
saving is substantial.

### F4. A 16-bit transposition move cache

The current key is unsafe for returning a score. It is safe as a hint if a probe only promotes
a stored move that is found in the generated list and the search still evaluates every move.
A collision then changes ordering, never supplies an unrelated score or illegal move.

Before target code, instrument 32-, 64- and 128-entry common-size caches on the host and report:

- probes, occupied probes and matching locks;
- stored moves found in the generated list;
- useful cutoff-first hits;
- whole-book nodes and completed depths at every budget.

RAM size is still Stefan's decision. No per-target sizes.

### F5. Aspiration windows

Use the prior iteration score, and repeat with the full window on either failure. A completed
iteration must return the baseline result. PVS's 5–6% ceiling is a reason for low expectations,
not permission to skip the cheap screen.

---

## 9. When the full transposition table may be reopened

Phase 19 closed the TT in the current engine. Do not write table code because an assembly
listing suggests the first price might fall. Reopen the question only if all of these occur:

1. B1–B6 materially reduce the shipping 16-bit hash cost for their own speed case.
2. A new 32-bit shipping build is priced under VICE over identical nodes and comes back below
   the 10% human-decision threshold with room left for probe/store overhead.
3. A host-only table-size instrument shows that the one common size the tight targets can fund
   has useful completed-depth hits. Do not assume 64 entries are useful or useless.
4. Stefan answers the memory, same-chess and visualizer/Atari questions at that time.

Then the original requirements return: dual switch, clear determinism invariant, fail-hard
exact/lower/upper bounds, ply-relative mate scores, switch-off equivalence, whole-book screen,
all-target play, gauntlet before Sargon, and strength rather than node savings as the decision.

A four-byte Zobrist table can compute one address and load four bytes but doubles the 1,584-byte
constant cost. A second independently permuted lookup reuses ROM but costs cycles and must be
tested for key quality. Neither is silently chosen for Stefan by whoever implements it.

---

## 10. Memory work that does not remove a feature

Measure before relying on either idea.

### Pack the persistent undo ring

The eight-byte entry appears reducible to six without reducing the 127-move capacity:

- move flags use seven bits, leaving bit 7 for mover colour;
- captured piece uses bits 0–2 and 7, leaving bits 3–6 for four castling rights;
- en-passant can be encoded as none or file 0–7 in four bits, with its rank reconstructed from
  mover colour;
- outcome fits the other nibble;
- halfmove remains one byte.

That would save 256 bytes of BSS. Undo, redo, log display, en passant, castling and wrapped-ring
tests have to prove the packing. It is off the search hot path, so clarity beats cycle count.

### Re-measure the move arena high-water mark

The arena once peaked at 231 and later 267 entries after the depth caps changed. It remains 512
because exhaustion silently loses strength in sharp positions. Instrument the full game corpus,
fuzzer, tactics and gauntlet; only a large margin can justify shrinking it. A 384-entry arena
would free 512 bytes, but a peak below 384 is not by itself proof that the unseen tail is safe.

Do not reclaim the visualizer or shorten undo history without bringing the explicit product
trade to Stefan.

### Atari upper RAM — undo ring relocated (2026-08-12)

`$AF00–$B41F` was 1,312 unused bytes between the GR.8 screen and the 2K stack. The 1,024-byte
persistent undo ring is now a `UNDOBSS` segment there (`#pragma bss-name` in `undo.c`, region
owned by `chessAtari.cfg`). Pointers stay in ordinary BSS. Other targets are unchanged.

Verified under AltirraBridgeServer, 800XL, XEX, and then on the MyPicoDOS ATR
(Stefan, complete play, flawless):

- load image `$1000–$7F49` — no write into the screen or the hole
- DLIST LMS `$9100` from title through menus, two-player, fool's mate and a 216-move AI game
- `c_sp` `$BC17` at boot, deepest `$BA08` during search — 1,801 bytes above the ring
- first two plies wrote `$AF00` = e2–e4 and `$AF08` = e7–e5; `U` / `R` restored both
- fool's mate delivered and taken back; `B` still draws the four numbers a square
- AI vs AI Very Easy played through a wrap of the 128-entry ring into a 5-piece ending

Framebuffer headroom is 363 → **1,387**. `DATA` still ends `$7CFB`, `DLIST` `$7D00`, 4 bytes
of page pad. 288 bytes remain unclaimed at `$B300–$B41F` as the stack pad. Chess is identical.

---

## 11. Ideas that remain on the floor, with their evidence

They are listed so "leave no idea behind" does not become "forget why it failed."

| Idea | Status |
|---|---|
| principal variation search | 5–6% at levels 3–4, worse at 1–2; below resolution |
| delta pruning | best 3.2%; stopped at the pre-gate |
| losing-capture skipping | best 13.7%; stronger dose 13.72%; stopped |
| null move | 27% / 22% saved, +0.04 sigma in gauntlet; rejected |
| full SEE for pruning | cheaper losing-capture mechanism could not remove enough nodes |
| late move reductions / futility / razoring | assumption-based pruning; four-for-four warning |
| pin set | about 7% estimated net, high correctness risk; Phase 5 rejection |
| pawn-shield king safety | -2.6 sigma; closed |
| queen d1 PST change | -1.09 sigma; stronger reverse dose +0.40 sigma; mechanism inert |
| E2 constrained value fit | existing N/B/R/Q + PST scales already at the train/val minimum; bishop −5 is noise |
| queen-before-minors (E3) | dose 16 +1.95σ / +1.67σ, short of +2σ; dose 48 worse; Atari page |
| incremental doubled/isolated (E1) | board-scan fits Atari when on; equal nodes +0.9σ; equal time −2.5σ at host 1.38×; rejected |
| KBN colour corner drive (E4) | L4 can convert when on; L1–3 0/8; Atari overflow; rejected |
| one-ply check extension (E5) | L1 mate-in-one 11/12→10/12; Atari page; rejected |
| per-target optimization or table sizes | violates same-engine rule |
| `optspeed` on selected targets | violates uniformity and does not fit Atari |
| timing on host or perft | wrong instrument for 6502 search cost |

Full PV (F1) is tried and rejected. History, root-score ordering, aspiration and the
move-only cache are still untried, not endorsed. Check extension is tried and rejected
(E5). They stay behind work with a higher expected ceiling only as cheap screens
(F2–F5), not as the next strength bet.

---

## 12. Gate order and handoff format

### Pure speed candidate

1. Add a compile-time shipping switch; do not use `EVAL_TUNING` to price a node.
2. `make -B test` and the relevant focused suites.
3. Whole-book switch-off equivalence: all 1,024 searches identical.
4. Fixed middlegame C64 replay, identical nodes, interleaved runs.
5. Record all seven target builds and maps; record the Atari `DATA`/`DLIST` relation.
6. Run terminal, Apple II, Plus/4 and Atari play checks available here. `atmos`, `c64.chr` and
   CX16 still need the honest run-status statement from `AGENTS.md`.
7. Keep or revert on measured aggregate speed and size. Do not raise budgets in the same step.

### Chess candidate

1. Dual runtime/compile-time switch and live-switch proof.
2. Purpose-built defect test at every real skill budget.
3. Whole-book node/depth/move screen.
4. All-target build and available play checks.
5. `make -B test`, `match sanity`, then 512-game self-play as a screen only.
6. On-target node price and equal-time configuration.
7. Full Stockfish gauntlet: pooled +2 sigma, no level below -1 sigma.
8. Sargon L4 confirmation and categorical comparison by colour, conversion, fifty-move draws
   and distinct games.
9. Atari page price last, with strength evidence in hand.

Every completed experiment ends in `doc/rework-log.md` with the baseline/candidate commits,
switches, commands, raw W-L-D or timing counts, map deltas and why it landed or stopped. A
rejected measurement is complete work. Reverted code plus a durable number is preferable to a
plausible feature whose gates were skipped.

---

## 13. Recommended order

### Done through Phase E (do not reopen without new evidence)

1. ~~Current C64 component profile~~ (A / Phase 20).
2. ~~B1–B7 exact state~~ — survivors **B3+B4 only** (−17.2% C64).
3. ~~C1–C5 exact work outside hash~~ — no survivors; defaults off.
4. ~~E1 pawn structure~~ — rejected equal-time (Phase 30).
5. ~~E4 KBN~~ — rejected Atari size (Phase 31).
6. ~~E5 check extension~~ — rejected L1 mate floor (Phase 32).
7. ~~E6 mine failures~~ — instrument + sample (Phase 33); queen-move swings lead.
8. ~~Phase D budgets~~ — 400 / 1,200 / 18,000 / 65,000 (Phase 34).
9. ~~**E2** value tuning~~ — existing family already at the constrained minimum (Phase 35).
10. ~~**E3** queen before minors~~ — rejected (Phase 36).

### Still open, in cheap order

1. ~~**Stefan:** Phase D for B3+B4~~ — L1/L2 banked time; L3 18k (ladder up); L4 65k (noise).
2. ~~**E2** locked-set value tuning~~ — existing numbers already at the constrained minimum (Phase 35).
3. ~~**E3** opening interaction~~ — rejected; 16 short of +2σ, 48 worse (Phase 36).
4. **§10** undo-ring *pack* / arena high-water — only if a slim KBN/ca65 needs another 256–512.
   The Atari upper-RAM claim is done: undo ring at `$AF00–$B2FF`, 1,387 free below `$9100`.
5. **F1–F5** as cheap whole-book screens — **F1 rejected** (Phase 38).
6. Reopen a full TT only through §9.
7. Reopen E1/E4 only with a free or near-free mechanism (true incremental that fits; ca65 KBN
   under ~300 CODE; or a measured memory reclaim that funds them).

This order is not a claim that the first idea wins. It is the cheapest sequence that can find
out, stop cleanly, keep every target playing the same chess, and leave the next person a result
rather than another premise.
