# cc65 Chess — Engine Rework Plan

> **What this document is.** The working journal the engine rework was planned and run
> from, written before the work started and updated as each phase landed. It is published
> as-is rather than tidied into a report, because the interesting part is not the plan —
> it is the places where a measurement arrived and demolished it.
>
> Read it for that. The hot path was predicted wrong and the prediction is still here with
> the profile that killed it. A pin-set optimisation was designed, argued for, and then
> dropped on evidence. Two evaluation terms were accepted on one measurement and removed on
> a better one. An overflow that no test on a desktop could ever reproduce was found by
> reading arithmetic against a 16-bit `int`. None of that was edited out.
>
> `doc/engine.md` describes the engine that came out of this and is the better starting
> point if you want to understand the code. `doc/strength.md` measures how well it plays.
> The durable rules distilled from the journal live in `AGENTS.md` at the repo root; this
> file is the history, not the standing instructions.
>
> Checkboxes are as they stood when the work paused: `[ ]` not done, `[X]` done. Phases are
> ordered by dependency, not by importance.

Phase 1 ends in a deliberate review gate — the first real measurement of engine speed on
8-bit hardware lands there, and several numbers in this document are expected to move as a
result.

---

## 1. Goal

Turn this from a legal-but-weak chess program into a small C chess engine that actually
plays, while keeping the parts that make it pleasant: the attacker/defender visualizer,
the undo/redo history, the menus, and the 8-bit ports.

The engine is weak for one structural reason, not many small ones. `cpu_ScorePieceSubTree`
looks like minimax but has a branching factor of one — at each level it follows only the
single reply that a static heuristic ranked first, so it evaluates one predicted line per
root move and never sees a refutation the heuristic missed. Two further problems compound
it: the root only ever considers one move per piece (about 16 candidates out of 30-40
legal), and the scoring functions return *move desirability* rather than *position value*,
which is not a quantity minimax can validly accumulate across plies.

The fix is to replace the search and evaluation, and to stop using the Attack DB as the
legality oracle. Move generation, the platform layer, and the UI are not the problem and
mostly survive.

## 2. Ground rules

- **No backward compatibility.** There are no saved games and no persisted state. Formats,
  encodings and structures may change freely.
- **`plat.h` and the 0-63 tile numbering are a frozen interface.** The 0x88 representation
  stays inside the engine and converts at the boundary. This is a hard rule, not a
  preference — it is what makes the rule below safe.
- **`atari` and `cx16` are not built or tested here.** The tools live on a Windows machine.
  Do not delete them, do not knowingly break them, and do not edit their platform files
  speculatively. If the frozen-interface rule is honoured they should need no changes at
  all. They get built when convenient, not as a gate.
- **All remaining targets must build and play at the *end* of the plan.** Intermediate
  phases may leave individual targets broken or over budget. Do not contort the design to
  keep every phase shippable.
- **Keys must not change.** Cursor keys, RETURN, RUN/STOP, `M`, `B`, `A`, `D`, `U`, `R`
  keep their current meanings. It must feel like the same game. New functionality that
  needs a key is a red flag — raise it rather than inventing a binding. The four-item skill
  menu is the only difficulty control; there is no slider and none is to be added.
- **The visualizer is a feature, not an accident.** The `B` / `A` / `D` displays are the
  thing worth preserving most. They may be reimplemented, but not lost or degraded.
- Development happens against the terminal and native builds. Port and measure at the end,
  except for the Phase 1 speed measurement, which happens early and on purpose.

## 3. Performance target

The reference machine is **real 1 MHz hardware at 1x**, measured in VICE (or a2m at 1x).
Emulator acceleration then multiplies everything for free and needs no separate budget.

Time per move, by skill level:

| Level | Target on real hardware | Notes |
|-------|------------------------|-------|
| 1 — Very Easy | 3-5 s | must stay snappy; this is the level someone learns on |
| 2 — Easy | 15-20 s | the agreed ceiling for a comfortable unaccelerated game |
| 3 — Harder | above that | emulator territory |
| 4 — Very Hard | above that | emulator territory |

Splitting the agreed 15-20 s across levels 1 and 2 rather than putting it all on level 1
was a judgement call, on the grounds that the easiest setting should be pleasant to play
against rather than merely weak. Flip it if that turns out wrong in practice.

Budgets are expressed as **fixed node counts**, not wall-clock time. That keeps behaviour
identical and deterministic across every platform, none of which share a timer, and lets
emulator speed-up work without any code knowing about it. The node counts are derived once
from a measured nodes-per-second figure.

**Measured at the end of Phase 1** (see the review gate item): the new core manages
**108-132 perft nodes/sec** on a real C64, PAL to NTSC. A search node costs a little more
than a perft node because of the evaluation, so budget conservatively at **~80-100
nodes/sec** until Phase 2 can measure the real thing.

That gives, at an alpha-beta effective branching factor of about 6:

| Level | Time | Node budget | Expected depth |
|-------|------|-------------|----------------|
| 1 | 3-5 s | ~300-400 | 3 ply, 2-3 with quiescence |
| 2 | 15-20 s | ~1300-1900 | 4 ply, 3-4 with quiescence |

Set the real budgets from a measured nodes/sec once the search exists, rather than from
this table — the table is what to expect, not what to hard-code.

**The hot path is `eng_IsAttacked`, not move generation.** Move generation is only about
8% of perft time; the rest is the per-move make / attack-test / unmake. The standard fix
is not to micro-optimise the ray-cast but to stop calling it: with the king not already in
check, only king moves, en-passant captures and moves by pinned pieces can leave the king
in check. Work the pin set out once per node from eight rays off the king, and most moves
need no legality test at all. That is plausibly worth a multiple, and a multiple is worth
a ply. Phase 2 should build for it and Phase 5 should do it.

> **Measured in Phase 5, and wrong — see the profile there.** This holds for *perft*, where
> every generated move is made and tested, which is why the paragraph reads so confidently.
> It does not hold for the *search*, which tries only 2.28 moves per generating node before
> a cutoff and never tests the rest. On a real C64 the search spends **42% in move
> generation against 13.7% in legality** — the exact inverse. The pin set was dropped as a
> result. The paragraph stays because the mistake is the instructive part: a cost measured
> on perft does not transfer to a search that abandons most of its moves unexamined.

`gDeepThoughts` disappears entirely. It exists only because legality testing currently
costs a full 64-square Attack DB rebuild; once legality is a ray-cast, there is no
inaccurate-but-fast mode to fall back to.

## 4. Target architecture

- **0x88 board**, 128 bytes. Off-board tests become `sq & 0x88`, which removes every
  `x > 7 || y > 7` unsigned-wraparound check in the generator and a class of bug with it.
- **Piece byte** keeps colour + type. `PIECE_MOVED` is retired: castling rights become a
  4-bit mask and the en-passant square a byte, both held in per-ply state. Pawn double-step
  is already decided by rank, not by the flag.
- **Make/unmake** with a per-ply state stack (captured piece, ep square, castling rights,
  halfmove clock — around 5 bytes a ply) replaces "make the move, rebuild the world, look,
  roll back".
- **`isAttacked(square, side)`** by ray-cast from the square: eight sliding directions plus
  knight offsets, pawn and king. Roughly forty byte reads, against a full 64-square Attack
  DB regeneration today. This is the change that buys the search depth.
- **The Attack DB stops being a stored structure.** `gAttackBoard` (2176 bytes) and
  `giAttackBoardOffset` (256 bytes) go away; the visualizer calls `attackersOf(square)` on
  demand. The `A`/`D` displays need one query; the `B` overlay needs 64, once per redraw,
  which is affordable. This reclaims roughly 2.4 KB of RAM — more than the search needs.
- **Search**: iterative deepening negamax with alpha-beta, MVV-LVA capture ordering, killer
  moves, and a quiescence search over captures.
- **Evaluation**: material plus piece-square tables, a pawn-shield king-safety term, and
  cheap pawn-structure terms. It must be a pure function of the position.

Deleted outright: `board_CheckForMate`, `board_UpdateAttackGrid`, `board_CheckLineAttack`,
`si_fixupTable`, and essentially all of `cpu.c`. Checkmate stops being something detected
by special-case machinery — it is simply "no legal moves and in check". Roughly 750 lines
come out against maybe 400 going in, so the binary should not grow and may well shrink.

Explicitly deferred, not rejected: an **opening book** (competes with the tightest memory
budget, and would reduce Phase 4 test coverage by making self-play games repeat their
openings) and a **transposition table** (affordable now that the Attack DB is gone, but
primarily a speed win, and speed is elastic once emulator acceleration is in play).

## 5. Working agreements

- Tabs for indentation, matching the existing files. Match the surrounding comment voice —
  explanatory, lower-case, no ceremony.
- C89 declarations (top of block). `char` for nearly everything; cc65's `char` is unsigned
  and `int` is 16-bit. Watch for expressions that silently need more than 16 bits.
- No new stdlib dependencies in code that reaches the 8-bit builds. Test-only helpers
  (FEN parsing, perft drivers) must be excluded from the cc65 build.
- Every phase ends with the test suite green. Do not carry a red suite forward.

Build and test commands:

```bash
make OPTIONS=optspeed TARGETS=c64
```

```bash
cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c src/search.c src/board.c src/undo.c src/cpu.c src/human.c src/frontend.c src/main.c src/term/platTerm.c -o /tmp/chessterm
```

```bash
cd tests && make test
```

**Measuring on real hardware.** `tests/vice-run.sh <prog.prg> [out.png]` runs a cc65 build
headless under VICE and leaves a screenshot to read the numbers off. Three things about it
are not obvious and are documented in the script: VICE finds its ROMs through a `./data`
symlink and ignores `VICE_DATADIR`; the macOS build needs GTK environment variables set by
hand; and `-warp` does *not* distort a measurement, because the jiffy clock counts emulated
time. Use `-ntsc`, since cc65's `CLOCKS_PER_SEC` of 60 is only true there.

Two practical notes learned the hard way. **Set `-limitcycles` to fit the job.** The default
1.2e9 is far more than most benchmarks need, and the emulator faithfully executes the
program's idle spin loop until it expires — three quarters of a typical run was waiting for
nothing. And **the VICE window takes focus when it opens**; typing into it breaks the
autostart. There is no flag for that on macOS GTK. The durable fix is to stop needing the
screen at all (see the note on symbols below).

On-target programs, all built the same way:

| Program | Measures |
|---------|----------|
| `tests/c64perft.c` | raw move generation |
| `tests/c64search.c` | search cost at three fixed budgets, from the opening |
| `tests/c64level1.c` | per-move time through a real game at the level 1 setting |
| `tests/c64skill.c` | per-move time and depth reached, per skill level, over 20 plies |
| `tests/c64profile.c` | where search time goes, by doubling one component at a time |

```bash
cd tests && cl65 -t c64 -Oris -I../src -o c64search.prg ../src/engine.c ../src/eval.c ../src/search.c c64search.c && ./vice-run.sh c64search.prg /tmp/out.png 400000000
```

**Do not measure from the opening position.** It is cheaper than the middlegame, not dearer
— see the level 1 finding in Phase 5. Anything that has to hold at the board gets measured
over a real game.

**Reading numbers off a screenshot is the weakest link in all of this.** Every on-target
figure in this document was ultimately read from a PNG by eye, and the images need upscaling
before they are legible. `ld65 -Ln labels.txt` emits VICE-format symbols for a cc65 build,
which would let a benchmark write its results into a named struct and have them read out of
memory over VICE's binary monitor instead: no screenshot, no spin loop, no cycle-limit
guess, and no window to steal focus. Not needed for anything outstanding, but it is the
obvious next improvement to the instrument, and it would also make count-only breakpoints
available for profiling — those cost host time rather than emulated cycles, so unlike
instrumenting the C they do not perturb the measurement they are explaining.

**Driving the real UI.** `tests/driveterm.py` runs the curses build under a pty and reads the
screen back, with canned scripts for startup, AI-vs-AI, the attack overlay, and a human move
followed by an undo. The unit tests cover the engine; this covers whether the game is
playable.

Known broken before this work started, and not caused by it: `atari` fails at link with an
unresolved `__INIT_LOAD__` from `src/atari/chessAtari.cfg`, and `cx16` fails to assemble
`src/cx16/platCX16.c` with an address-size range error. Both look like cc65 version drift.
Neither is in scope; see the ground rules.

---

## Phase 0 — Safety net

Nothing else in this plan is safe without this. Do it first and do it properly.

- [X] Move the throwaway test harness from the scratchpad into `tests/` in the repo: the
      stub platform layer, the position-snapshot fuzzer, and the castling scenario matrix.
      Give it a one-command build and a single entry point that returns non-zero on
      failure, so every later phase can run it in one step.

- [X] Add a FEN parser and a `perft` driver to `tests/` only. Perft — counting leaf nodes
      to a fixed depth — is the standard correctness test for a move generator and the only
      way to know the new one is right. Keep both out of the cc65 build entirely.

- [X] Record perft baselines for the *current* generator against the standard positions,
      and note every disagreement rather than fixing it. **Result:** all five reference
      sets verified against chessprogramming.org and baked into `tests/perft.c`. The
      generator is *correct* — it matches the reference exactly to depth 5 on the initial
      position and on position 3, and to depth 3 on Kiwipete, which is the position built
      to catch castling and en-passant mistakes. That was better than expected and it is
      why Phase 1 can port the generation rules rather than rederive them.

      There is exactly **one** divergence, and it has a single cause: **the engine never
      generates an under-promotion.** A pawn reaching the back rank is one move in the
      generator; `board_ProcessAction` then picks the piece — always a queen for the AI, a
      menu prompt for a human. So every promoting move counts 1 where the reference counts
      4. At depth 1 the arithmetic is exact ("middlegame" has one promoting move and is
      exactly 3 short); deeper, the missing move takes its subtree with it. This is why
      Kiwipete only diverges from depth 4, where a promotion first becomes reachable.
      Phase 1 must make promotion a flag on the move rather than a decision taken during
      make, at which point every one of these baselines should snap to the expected column
      and report IMPROVED.

- [X] Add a self-play harness that runs engine-vs-engine games natively and reports
      result, average nodes, and average time per move. This is how strength changes get
      measured in Phase 4 — without it, tuning is guesswork.

## Phase 1 — New board core

Replace the board representation and legality machinery. The UI will be broken for the
duration of this phase; that is expected and acceptable.

- [X] Convert the board to 0x88 and retire `PIECE_MOVED` in favour of a castling-rights
      mask plus an en-passant square held in per-ply state. Keep the 0-63 tile numbering at
      the `plat.h` boundary per the ground rules, converting on the way in and out, so no
      platform file needs touching.

- [X] Write `isAttacked(square, side)` as a ray-cast, and `attackersOf(square, list)`
      returning the attacking squares for the visualizer. These two functions replace
      every current use of `gAttackBoard`.

- [X] Write make/unmake against a per-ply state stack. Make applies the move including
      castling, en passant and promotion; unmake restores exactly, including rights and
      the ep square. This is the single most correctness-critical pair of functions in the
      engine — treat a perft mismatch here as a stop-the-line event.

- [X] Write legal move generation for a side into a per-ply slice of a shared move arena.
      Two-byte move encoding (from, to, flags) keeps the arena small enough to live
      comfortably in 8-bit RAM. Generate pseudo-legal, then filter by make + `isAttacked`
      on the king + unmake.

- [X] Get perft passing on every reference position to at least depth 4, depth 5 where it
      is affordable natively. **Result: exact on all five positions to depth 5**, including
      193,690,690 nodes on Kiwipete, and exact to depth 6 on the two positions that publish
      one (119,060,324 and 11,030,083). Zero divergence — the under-promotions the old
      generator never produced are now generated, so the Phase 0 baselines are all
      superseded. Natively it is also **32x faster** than driving the old engine the same
      way (Kiwipete depth 5: 4.3 s against 139 s), which is the make/unmake and ray-cast
      change showing up exactly where it was predicted to.

- [X] Delete `gAttackBoard`, `giAttackBoardOffset`, `board_CheckForMate`,
      `board_UpdateAttackGrid`, `board_CheckLineAttack` and `si_fixupTable`. **Done in
      Phase 3, with the switchover.** Measured on the c64 at `optspeed`:

      | Segment | Before | After | Delta |
      |---------|--------|-------|-------|
      | CODE    | 25079  | 24020 | **-1059** |
      | RODATA  | 1798   | 2235  | +437 (piece-square tables) |
      | DATA    | 348    | 344   | -4 |
      | BSS     | 4318   | 3808  | **-510** |
      | Total   | 31543  | 30407 | **-1136** |

      So the whole rework — 0x88 board, make/unmake, alpha-beta, quiescence, move
      ordering — is **1136 bytes smaller than what it replaced**, and uses 514 bytes less
      RAM. All five buildable targets fit again, including the two that had been over
      budget while both engines were linked.

      The original wording is kept below because the reasoning for deferring it still
      applies to anything similar: pulling the old machinery before its replacement was
      wired gave a tree where nothing linked and no delta could be measured. The old mate
      machinery is only reachable through `board_ProcessAction`, which `cpu.c` and
      `human.c` still depend on, so pulling it now yields a tree where nothing links and
      no delta can be measured. It comes out with `cpu.c` in Phase 2, where the number is
      real.

      **This is not free in the meantime.** The root Makefile globs `src/*.c`, so adding
      `engine.c` puts *both* engines in every target, and the two tightest do not fit:
      apple2 overflows BSS by 994 bytes and atmos by 250. Both build again the moment
      `engine.c` is set aside, so the cause is only the transitional overlap. It resolves
      itself in Phase 2 with room to spare — `gAttackBoard` and `giAttackBoardOffset`
      alone are 2432 bytes of BSS, before any of the old code goes.

- [X] **Review gate: measure real 8-bit speed.** Built `tests/c64perft.c` against the new
      core and ran it under VICE. Warp mode does not distort anything, because the jiffy
      clock counts emulated time. Method and raw numbers are in `tests/c64perft.c`.

      **perft(3) from the initial position, 8902 leaf nodes:**

      | Model | Jiffies | Seconds | Leaf nodes/sec |
      |-------|---------|---------|----------------|
      | NTSC  | 4019    | 67.2    | 132 |
      | PAL   | 4137    | 82.5    | 108 |

      (cc65 reports `CLOCKS_PER_SEC` as 60 for the c64, which is only right on NTSC — the
      PAL jiffy ticks at about 50 Hz. Both models were run rather than picking one.)

      Counting every move examined rather than only the leaves — 20 + 400 + 8902 = 9322
      make / attack-test / unmake sequences plus 421 move generations — gives **113-139
      moves examined per second**, or about **7400 cycles each**, all in.

      **This beats the 30-50 nodes/sec working estimate by 2.5 to 4x**, so the
      representation is sound and Phase 2 proceeds unchanged.

## Phase 2 — Search and evaluation

- [X] Write `eval(side)` as a pure function of the position: material first, then
      piece-square tables. Nothing else can be built until the engine has a position value
      rather than a move-desirability score, so keep this first and keep it simple.

- [X] Replace `cpu.c` entirely with negamax plus alpha-beta. Checkmate and stalemate fall
      out of "no legal moves" plus the in-check test — `search_Outcome` is nine lines and
      replaces `board_CheckForMate`, `board_UpdateAttackGrid`, `board_CheckLineAttack` and
      `si_fixupTable` outright. **The new search exists and is validated; the old `cpu.c`
      is still linked** because deleting it needs the game to be driving the new board,
      which is the Phase 3 switchover.

      Note for anyone porting: cc65 allows a function only 256 bytes of locals, so a
      `t_engMove[128]` on the stack does not build even though the host compiler accepts
      it. Move lists come from the shared arena, never the stack.

- [X] Add MVV-LVA capture ordering and two killer moves per ply. Alpha-beta without move
      ordering is barely better than plain minimax — this is not optional polish, it is
      what makes the pruning work.

- [X] Add quiescence search over captures at the horizon. This is the largest
      strength-per-byte item after alpha-beta and it is what stops the engine hanging
      pieces at the leaf.

- [X] Add iterative deepening driven by the node budget from Phase 1, so a move is always
      available whenever the budget runs out and behaviour is identical on every platform.
      Map the four skill levels onto (depth cap, node budget) pairs, with the depth cap as
      the primary weakener and the node budget as the safety valve, so the existing skill
      menu keeps working unchanged. `gSkill` and `gDeepThoughts` retire here.

- [X] Confirm via self-play that the new engine beats the old one decisively. **Result:
      6 wins, 0 losses, 0 draws** against the old engine at its strongest setting, mating
      it on ply 31 as white and ply 52 as black. Note the games are deterministic on both
      sides, so N games is one game repeated N times — Phase 4 needs opening variety
      before self-play can measure anything finer than this.

### Phase 2 measurement: what a search node actually costs

Measured on a real C64 with `tests/c64search.c`, searching the opening position (the
conservative case — a thinner middlegame searches faster):

| Node budget | Depth reached | Seconds | Nodes/sec |
|-------------|---------------|---------|-----------|
| 400   | 2 | 16.4  | 24 |
| 1600  | 3 | 38.4  | 42 |
| 6000  | 4 | 277.7 | 22 |

**A search node costs about 4x a perft node** — 22-42/sec against perft's 132/sec. The
Phase 1 assumption of "a little more than a perft node" was wrong, and the budgets in
section 3 were correspondingly optimistic. `gcSearchSkill` in `search.c` is set from these
measurements instead.

So on bare metal the agreed targets buy **depth 2 at level 1 and depth 3 at level 2**, not
the 3 and 4 projected. Depth 4 is 4.6 minutes. Everything deeper is emulator territory,
which is the arrangement that was expected anyway — but it is a ply short of the estimate
and worth knowing before Phase 4 starts tuning.

Three concrete reasons, in the order they are worth fixing:

- **Quiescence asks for every move and then throws away the quiet ones.** A quiescence
  node pays for a full move generation and ordering to search maybe two captures. This is
  why the nodes/sec figure gets *worse* as the search goes deeper — deeper means
  proportionally more quiescence. A capture-only generator is the single biggest win here.
- **`eval_Position` walks all 64 squares every time it is called.** Material and
  piece-square scores can be updated incrementally in `eng_Make` and `eng_Unmake` instead,
  turning the evaluation from a loop into an add.
- **Legality still costs an `eng_IsAttacked` per move**, the Phase 1 finding. Working out
  the pin set once per node removes most of those calls.

Together these are plausibly worth 2-4x, which is a ply. They are Phase 5 items but the
measurement says they are load-bearing, not polish.

## Phase 3 — Reconnect the UI

- [X] Rebuild undo/redo on the new move records. The per-ply state the search already
      saves is very close to what undo needs; prefer one representation over two. Keep the
      user-visible behaviour identical, including undoing two plies in a human-vs-AI game.

- [X] Reimplement the `B`, `A` and `D` visualizer displays on `attackersOf()`. This is the
      feature that matters most in the whole program — verify it against the old behaviour
      case by case, including the toggle-persistence logic that re-shows attackers when the
      cursor moves.

- [X] Reconnect cursor colouring, the move log, the promotion menu and the side-to-go
      label. Verify every key still does exactly what it did: cursor keys, RETURN,
      RUN/STOP, `M`, `B`, `A`, `D`, `U`, `R`.

- [X] Play full games through the terminal build in all four modes (human vs AI both
      colours, human vs human, AI vs AI) and confirm nothing in the presentation regressed.

### Phase 3 findings

**The frozen interface is wider than `plat.h`.** Every port also reads three globals
directly, and that was not in the original ground rule. The full contract turned out to be:

- `gChessBoard[y][x]` — pieces, for drawing
- `gpAttackBoard[giAttackBoardOffset[tile][side]]` — the attacker **count** only; no port
  ever reads the attacker list
- `gTile[0]`, `gTile[1]`, `gPiece[1]`, `gColor[0]` — the move log line, filled in by
  `undo_FindUndoLine`

All of it was honoured, so **no platform file was edited at all**, including the three
that cannot be built here. `gChessBoard` is now a 64-byte display mirror refreshed by
`board_SyncDisplay`, and the 2176-byte attack database became a 128-byte count array
computed on demand — and only when the B display is actually switched on.

**Level 1 is weaker than the budget suggests.** Its 150-node budget completes depth 1 and
aborts during depth 2, so it plays a one-ply game: it grabs material and little else. In an
AI-vs-AI game at level 1 the engine shuffled its king and drew by the fifty-move rule.
Completing depth 2 needs roughly 400 nodes, which is 16 seconds — over the 3-5 second
target. So **the level 1 target cannot be met at depth 2 until the Phase 5 optimisations
land**; either level 1 stays a one-ply engine for now, or its time budget goes up.

**There is no repetition detection.** Nothing stops the search preferring to shuffle, and
the fifty-move rule is the only thing that ends it. Worth adding in Phase 4 — it is cheap
next to what it prevents.

**A note for anyone writing a generator that fills a shared arena:** the first version
wrote the moves and *then* checked whether they fitted, which overran the arena into the
statics that followed it — `si_nodes`, `si_budget` and `sc_abort`. The symptom was the
node budget being silently ignored, which looks nothing like a buffer overrun. The
generator now takes its capacity as an argument and cannot write past it.

## Phase 4 — Strength

Everything here is measured by self-play against the previous revision. Keep changes that
win, revert changes that do not, and do not trust intuition over the harness.

- [X] Tune the piece-square tables, especially pawns and king. Consider mirrored tables so
      one set serves both colours, and folding knight and bishop onto a shared table if
      space gets tight.

- [X] Add a pawn-shield king-safety term and cheap pawn-structure terms — passed, doubled,
      isolated. Keep each behind a measured win.

- [X] Re-tune the four skill levels against the Phase 3 time targets so level 1 is
      genuinely beatable by a beginner and level 4 is a real fight. Weakening should come
      from the depth cap or deliberate move-choice noise, not from a degraded evaluation.

### Phase 4 findings: the instrument, and what it overturned

**Building the instrument first was not optional.** `tests/match.c` plays two configurations
over a generated opening set — a few plies of random legal non-capturing moves from a fixed
seed per opening — each opening twice with the colours swapped. Generated rather than
hand-written, because a page of typed FENs is a page of transcription errors. It is
validated by having a configuration play *itself*: that must come out exactly level, and it
does, at every sample size.

**Sixteen games tells you nothing, and it lies confidently.** The same three comparisons at
three sample sizes:

| Comparison | 16 games | 128 games | 512 games |
|------------|----------|-----------|-----------|
| +pst vs material only | 9-1-6 | 70-5-53 | overwhelming |
| +king safety vs +pst | 4-2-10 *(looks good)* | 28-39-61 | **104-145-263, -2.6 sigma** |
| +pawn struct vs +pst | 6-2-8 | 37-28-63 | **132-102-278, +2.0 sigma** |

At 16 games king safety looked like a mild win. It is a real loss. Anything under a few
hundred games cannot separate a two-sigma effect from noise, so **do not accept a tuning
result from a short match** - the runs are cheap, 512 games is about 35 seconds.

**Results, and what was kept:**

- **Piece-square tables: kept.** 138-10-108 over 256 games, about ten sigma. Not a close call.
- **Pawn structure (doubled, isolated, passed): kept.** +2.0 sigma over 512 games.
- **King safety (pawn shield): removed.** -2.6 sigma over 512 games, and removing it from
  the full set is itself a measured win at 170-135-207. The first guess was that it lacked
  an endgame phase check, so a material gate was added - and it made no difference, which
  killed that theory too. The lesson is narrower than "king safety is bad": *counting pawns
  in front of the king* is bad. A term based on how many enemy pieces bear on the squares
  around the king may still be worth trying, and the comment in `eval.h` says so, so nobody
  re-adds the pawn-shield version by accident.
- **Depth: confirmed sound.** Depth 4 beats depth 2 by 46-2-16. If that had been close it
  would have meant a broken search rather than a weak evaluation.

**The equal-time test is the one that decides, and it changed every answer.** The match
compares configurations at equal *node counts*, but a richer evaluation buys its strength by
making every node slower, and on a 1 MHz machine that is not a rounding error:

| Term | At equal nodes | Cost per node (measured on a C64) | At equal time |
|------|----------------|-----------------------------------|---------------|
| Pawn structure | +2.0 sigma | 1.35x slower | **+0.6 sigma - nothing** |
| Endgame king table | +1.9 sigma | 1.28x slower | not worth measuring |

Both were removed. Both are *good terms* - the measurements say so at equal nodes - and both
are blocked on the same thing: **the evaluation is recomputed from scratch at every node**, so
anything added to it is paid for twenty thousand times a move.

One dead end worth recording, because it looked obvious and was wrong: the phase count for
the endgame king table was first written as an `int` sum of material, and the theory was that
16-bit adds were the expense. Rewriting it as a `char` count changed the timing by one jiffy
in three thousand. The cost is not the arithmetic width, it is doing *anything at all* per
piece, thirty-two times, at every node.

This is now the strongest argument for the Phase 5 work, and it is evidence rather than
intuition: **making the evaluation incremental is not an optimisation, it is what unblocks
the evaluation from being improved at all.** Two measured improvements are sitting behind it.

**Skill levels, re-tuned** from the measured C64 node costs, and each verified to beat the one
below it over 96 games:

| Level | Depth | Nodes | On a stock C64 | vs the level below |
|-------|-------|-------|----------------|--------------------|
| 1 | 2 | 500 | ~18 s | — |
| 2 | 3 | 2000 | ~45 s | 20-4-72 |
| 3 | 4 | 7000 | minutes | 49-11-36 |
| 4 | 6 | 30000 | emulator territory | 59-9-28 |

Level 1 now completes depth 2 rather than abandoning it, which was the Phase 3 finding.

### Watch the cost of test scaffolding

Two regressions were introduced by changes made for testability and safety, and both were
invisible until measured on the target. At budget 1600 the search went from 2304 jiffies to
2838 - a 23% slowdown - with no evaluation term added:

- The `EVAL_HAS` switches introduced a temporary `int` per piece that cc65 keeps even when
  the tests fold away to constants. The shipping build now has the flat version under
  `#else` and only the tuning build pays.
- `eng_GenMoves` was refactored to call `eng_GenMovesFrom` per square, so a move generation
  made 64 calls where it used to run one inline switch. Testing for a piece before the call
  brings that back to about 16.

Now at 2454, or 1.065x the Phase 2 baseline. The remaining 6% buys the generator's capacity
argument, which is what stops it overrunning the arena. That is a fair trade and worth
keeping - but **anything added to the evaluation or the generator has to be measured on the
target, not assumed free.**

## Phase 5 — 8-bit fit and speed

- [X] Measure binary size and RAM for every buildable target and compare against the
      pre-rework baseline. The C64 baseline before this work was 27130 bytes at
      `optspeed`, 25883 at `optsize`.

      **Done, and there are now seven targets rather than five** — `atari` and `cx16` both
      build here too (Phase 6). Segment totals from `ld65 --mapfile`, after repetition
      detection:

      | Target | CODE | RODATA | DATA | BSS | Total `optsize` | Total `optspeed` |
      |---|---|---|---|---|---|---|
      | atari   | 21577 | 3335 | 287 | 4068 | **29267** | 31550 |
      | c64.chr | 22118 | 2783 | 291 | 5030 | **30222** | 32030 |
      | apple2  | 21630 | 4464 | 492 | 4016 | **30602** | 32788 |
      | cx16    | 22432 | 3820 | 341 | 4067 | **30660** | 32803 |
      | atmos   | 24080 | 2514 | 290 | 4085 | **30969** | 32944 |
      | plus4   | 23105 | 3695 | 324 | 4070 | **31194** | 33624 |
      | c64     | 23474 | 3819 | 341 | 4071 | **31705** | 34123 |

      (CODE and the totals are the `optsize` figures; RODATA, DATA and BSS do not move
      between the two settings.)

      Against the baseline, the c64 is **31705 against 25883 at `optsize`** — 5822 bytes
      more program for a search that actually searches, an incremental evaluation, and
      repetition detection. All seven link inside their budgets. The Apple II is the
      tightest by a wide margin at 1255 bytes spare in MAIN, and only because Phase 7 gave
      it a config that reclaims the RAM stranded below HGR page 1.

- [X] **Make the evaluation incremental.** `geEvalScore` is a running white-positive total
      kept up to date by `eng_Make` and `eng_Unmake`; `eval_Position` is now a read.
      `eval_Refresh` rebuilds it and is called wherever pieces reach the board without
      going through make — `eng_SetStartPosition`, the test FEN parser, and once per move
      at the top of `search_Best` so the search can never inherit a stale total.

      The rule that keeps it honest is worth stating on its own: `eval_MoveDelta` is a
      pure function of the move and the two piece bytes, and reads no board state at all.
      Make adds it, unmake subtracts *the same call*, so the pair cannot drift by
      construction rather than by care. `gamefuzz` checks the running total against a full
      recount after every move, undo and redo, over 300 games that prefer castling, en
      passant and promotion — the three moves whose delta is not simply "a piece left one
      square and arrived on another".

      **Measured on a real C64 (NTSC jiffies, opening position, `tests/c64search.c`),
      against the same tree with the change backed out:**

      | Budget | Depth | Before | After | Speedup |
      |--------|-------|--------|-------|---------|
      | 400    | 2 | 1071  | 944   | 1.13x |
      | 1600   | 3 | 2456  | 1897  | **1.29x** |
      | 6000   | 4 | 18095 | 16138 | 1.12x |

      (The 2456 reproduces the 2454 recorded in Phase 4, so the backed-out build is a
      faithful baseline and not an approximation.)

      **Costs 465 bytes** on the c64 at `optspeed`: CODE 24054 -> 24517, BSS 3808 -> 3810.

      Two things about this are worth carrying forward. First, the win is smaller than the
      "2-4x for the three items together" the Phase 2 note projected, and it varies with
      the shape of the tree: the delta is now paid on *every* make, including the roughly
      half that are legality probes immediately unmade, so make/unmake got dearer at the
      same time as eval got cheaper. Native perft, which is all make/unmake and no eval,
      is 1.29x *slower*. The pin-set work is what pays that back, because it removes those
      probe makes entirely.

      Second, and more important: **behaviour is bit-identical.** The 512-game match
      returns exactly 113-113-286 and self-play reports the same node counts and the same
      mates on the same plies as before the change. That is the result to insist on for
      this kind of rewrite — a pure speed change that alters a single game has a bug in it.

- [X] **Capture-only quiescence generation.** `eng_GenCaptures` generates captures, en
      passant and promotions directly, instead of quiescence asking for every move and
      discarding the quiet ones. Measured over the perft trees, **captures are 17% of all
      moves** (1,272,951 of 7,282,134), so the old arrangement paid for a full generation
      six times over to search what it kept.

      The generators are shared rather than duplicated — one static flag, and the
      captures-only pass walks the same squares in the same order and only declines to
      *emit* the quiet moves. That buys the property the whole change rests on: the result
      is an exact **subsequence** of what `eng_GenMoves` would have produced. That is not
      tidiness. `pickBest` breaks score ties by list position, so a generator that produced
      the right set in a different order would leave the engine playing different moves in
      tied positions — legal, plausible, and quietly not the engine that was measured.
      `tests/quiescence.c` checks set *and* order against the filtered full generator at
      every node of the perft trees: 185,939 positions, exact.

      **Measured on a real C64 (NTSC jiffies, opening position):**

      | Budget | Depth | Phase 4 | +incremental eval | +capture gen | Total |
      |--------|-------|---------|-------------------|--------------|-------|
      | 400    | 2 | 1071  | 944   | **529**  | 2.02x |
      | 1600   | 3 | 2456  | 1897  | **1299** | 1.89x |
      | 6000   | 4 | 18095 | 16138 | **8866** | 2.04x |

      **So the two Phase 5 items together are almost exactly 2x on bare metal.** Depth 4
      from the opening is now 148 seconds against the 278 recorded in Phase 2. Note that
      2x is *not* a ply — at an effective branching factor of about 6, a ply costs 6x. The
      Phase 2 note's "2-4x, which is a ply" was optimistic on that arithmetic.

      Costs **222 bytes** (CODE 24517 -> 24739, BSS +1). Phase 5 to date: +687 bytes for 2x.

      ### The part that was not in the plan: quiescence was running out of arena

      This change is *not* bit-identical, and the reason turned out to be worth more than
      the speed. Self-play is unchanged move for move, but the 512-game match moved from
      113-113-286 to 111-111-290 and the node count went *up*. Instrumenting the arena
      high-water mark explained it exactly:

      | | arena high water | times exhausted |
      |---|---|---|
      | Full generation in quiescence | **512 of 512** | **1018** |
      | Capture-only | 231 of 512 | 0 |

      A long capture chain took a full-width slice per ply and filled the arena, at which
      point `quiesce` bailed out to a static evaluation. Over one match workload that
      happened 1018 times — so quiescence was giving up **precisely in the sharp positions
      it exists for**, and the engine was quietly weaker than its node counts suggested.
      The extra million nodes in the new run are searches that used to be abandoned.

      Worth generalising: running out was never a *correctness* problem, because the
      generator is handed its capacity and stops. It was a silent strength problem, and
      those are much harder to notice than a crash. The 512 entries stay for now — the
      headroom is real and cheap; revisit it only if a target's RAM budget actually needs
      the 1.1 KB.

- [ ] **Reinstate the two deferred evaluation terms**, now that the thing blocking them is
      gone. This is not the free ride the Phase 4 note implies, and the reason is worth
      knowing before starting: `eval_MoveDelta` may only look at the move, so a term folds
      in cheaply *only if it is a property of a piece on a square*. The endgame king table
      qualifies once the game phase is also tracked incrementally (it changes only on a
      capture or a promotion). Pawn structure does not — doubled, isolated and passed are
      properties of the whole pawn configuration, so it needs a per-file pawn count carried
      alongside the score, updated on pawn moves, captures and promotions only. Each still
      goes behind a 512-game match at equal *time*, which is the test that overturned both
      of them the first time.

- [X] **Profile where search time actually goes on a real 6502.** Done, and it overturned
      the premise the remaining optimisation work was resting on. See below.

- [X] ~~Per-node pin set.~~ **Dropped on measurement, not on difficulty.** Closed as a
      decision rather than left open as work. See below.

- [X] Bring each target inside its budget. **Nothing to do — all five buildable targets
      link clean and fit.** The lever noted here was wrong anyway: the undo stack is
      `UNDO_STACK_SIZE` 128 in `undo.c`, not 255. If RAM ever does get short, the 2 KB
      `SEARCH_ARENA` is the better lever now that quiescence peaks at 231 of its 512 entries.

- [X] **Re-tune the per-skill budgets.** Done — see below. Every level now plays at least
      as deep as before, in a fraction of the time.

### Phase 5: the skill table, re-tuned

**The mistake in the old table was conceptual, not arithmetic.** `search_Best` stops when
it finishes its deepest iteration, not when the budget runs out — so in a normal position a
level spends what its *depth* costs and hands the move back. **The depth cap sets the time;
the budget is only a safety valve.** The old table was tuned as though the budget set the
time, which put three of the four levels in the trap where the budget runs out part-way
through an iteration: the whole budget is spent, the iteration is discarded, and the
shallower move is played anyway.

`tests/budget.c` measures the missing number — nodes to *complete* each depth, over a real
game, from ply 7 on. Node counts are platform independent because the search is
deterministic, so these are exactly what the 6502 does:

| Depth | Mean nodes | Worst | 
|-------|-----------|-------|
| 1 | 152 | 335 |
| 2 | 1159 | 3551 |
| 3 | 6720 | 14712 |
| 4 | 28868 | 51161 |

Against that, the old table reads as a catalogue of near-misses: level 1 had 500 nodes for a
depth needing 1159, level 3 had 7000 for a depth needing 28868, level 4 had 30000 for a
depth needing 41641+. **Level 4 never once reached the depth 6 it advertised.**

**The first attempt at the new table made the depth cap the weakener, and that was wrong
for a reason worth writing down.** It set level 1 to a hard cap of depth 1, which hit the
3-5 s target beautifully and **could not mate**. Reported from a real game: black up a
queen, a rook and a bishop against a bare king, shuffling a rook between d8 and e8 for
ninety-odd plies while the human walked his king around, heading for a fifty-move draw.
Reproduced exactly (`3r2k1/1pp3p1/p2b4/4q3/8/6p1/8/6K1 b`) — at depth 1 the engine plays the
rook shuffle, at depth 2 it makes progress, at depth 4 it announces mate in 3. Played out
with the human doing nothing but shuffling: **depth 1 never converts, halfmove 91 and
counting.**

**A cap is the wrong lever because what a depth costs depends on the position, and a cap
does not.** Depth 2 wants ~1159 nodes in a middlegame but only ~220 in an endgame. A cap
set to keep the middlegame quick therefore also blinds the engine in endgames where that
same depth was nearly free — which is precisely where the depth is needed to convert. A
*budget* scales the right way by construction: paired with `search_Best` declining to start
an iteration it cannot finish, it buys whatever depth the position can afford. Endgames get
searched deeper for nothing.

**Final table** — depth caps are a safety rail, budgets do the work. Times measured on a
real C64 over 20 plies (`tests/c64skill.c`):

| Level | Was | Now | Mean | Worst | Old cost |
|-------|-----|-----|------|-------|----------|
| 1 Very Easy | 2 / 500 | **3 / 400** | **8.2 s** | 12.5 s | ~14 s, and a ply shallower |
| 2 Easy | 3 / 2000 | **4 / 1200** | **29.4 s** | 39.4 s | ~62 s |
| 3 Harder | 4 / 7000 | **5 / 15000** | ~3.5 min | — | ~3.6 min |
| 4 Very Hard | 6 / 30000 | **6 / 60000** | ~15 min | — | ~15 min, reached depth 4 only 39% of the time |

Every level now mates the bare-king position that started this: 11 plies at level 1, 9 at
level 2, 3 at levels 3 and 4. Ladder re-verified at 512 games a rung: 195-43-274,
327-13-172, 225-25-262.

**Level 1's 400 is a floor, not a preference.** At 300 nodes it goes straight back to
shuffling the won endgame into a draw. So level 1 sits at 8.2 s against a 3-5 s target, and
that is a deliberate trade: **a "very easy" level that cannot beat a bare king is broken,
not easy.** Level 2's 29.4 s likewise misses 15-20 s. Both targets came from the Phase 1
projection of four ply in that window, which Phase 2 already showed to be two ply
optimistic — **revise the targets rather than mistune the levels to chase them.**

### A 16-bit overflow the native suite could not have caught

The iteration guard was first written as `(si_nodes + si_nodes + si_nodes) > si_budget`.
**cc65's `unsigned int` is 16 bits**, so that wraps above 21845 — and level 4's budget is
60000. Verified on the target compiler:

| si_nodes | 3 × nodes, as 16 bits | old test | correct |
|----------|----------------------|----------|---------|
| 5292 | 15876 | go | go |
| 20000 | 60000 | go | go |
| **28868** | **21068** | **go** | **STOP** |
| **45000** | **3928** | **go** | **STOP** |

So at level 4 the guard **never fired at all**, and the search started a depth-5 iteration it
could not finish, burning some 31000 nodes — roughly doubling the level's time for nothing.
Precisely the waste the guard exists to prevent, present only on the target.

The host build has 32-bit ints, so **no native test can ever reproduce this**: the whole
suite, the ladder, the match harness and self-play all pass either way. It was found by
re-reading the arithmetic against the working agreement about 16-bit expressions, not by a
failing test. Fixed by dividing instead of multiplying — `si_nodes > si_budget / 3` is
arithmetically identical and cannot overflow.

Worth generalising: **the native suite validates logic, never machine width.** Anything that
multiplies, accumulates, or sums toward a limit needs reading against a 16-bit `int` by hand,
or checking with a throwaway `cl65` program — the one used here took a 30-second VICE run.

Two supporting changes in `search_Best`, both needed before budgets could be set honestly:

- **It will not start an iteration it cannot finish.** Measured growth is about 5x a depth;
  the test uses a cautious 3x, so a depth that might just make it still gets its chance.
  This is where most of the speed above comes from, and it means an over-generous budget
  costs nothing rather than being burnt on a discarded iteration.
- **A move is always returned when one exists.** See the bug below — this started as a
  theoretical tidy-up, and turned out to be a live defect that had already reached a board.

### The AI that gave up: a bug, and a lesson about "worst case"

**Symptom, from a real game on a C64:** human vs AI, human played, and the turn came
straight back — no *Think* message, no AI move, the side-to-go label unchanged. The AI
appeared to resign a perfectly sound position.

**Cause.** `search_Best` returned `m_haveMove = 0`, which every caller reads as "no legal
moves", i.e. mate or stalemate. It did that because the node budget ran out before depth 1
finished. Reproduced exactly from the screenshot position
(`r2qkb1r/ppp2ppp/2n1bn2/3pp3/3PP3/2N1BN2/PPP2PPP/R2QKB1R b`): **depth 1 alone wants 1404
nodes there**, against level 1's budget of 500.

**Three things about this are worth keeping.**

*First, the "worst case" in the table above was nothing of the kind.* It said depth 1 costs
152 nodes on average and 335 at worst. That 335 was the worst of eighteen positions from a
single quiet self-played game — a sample, presented as a bound. Measured over genuinely
sharp positions, depth 1 alone costs:

| Position | Nodes for depth 1 |
|----------|------------------|
| The bug report position | 1404 |
| Kiwipete | 3228 |
| Promotion test | 869 |
| Quiet middlegame | 115 |
| Opening | 40 |

An order of magnitude above the "worst" on record. **A maximum taken from one self-played
game is not a maximum**, because the engine plays quiet moves and then samples the quiet
positions it created.

*Second, the first attempt at the fix did not fix it.* Keeping the partial result in
`search_Best` only helps when at least one root move finished before the abort. In this
position the budget died inside the *first* move's subtree, so nothing had been recorded —
and worse, `searchRoot` ended with `if(!legal) result->m_haveMove = 0;`, which actively
wiped a banked move, because `legal` counts searches that *completed* rather than legal
moves that *exist*. The working fix banks a legal move the moment legality is established,
before searching it, and drops that reset.

*Third, the shape of the failure was the giveaway.* Running out of time is normal and must
degrade to a **worse move**, never to **no move**. Any code path where exhausting a budget
produces "nothing" instead of "something rougher" is wrong by construction, whatever the
numbers say. `tests/search.c` now checks the guarantee over four sharp positions at budgets
down to a single node — far below anything the skill table would ever use, because the
property has to hold at any budget, not just the shipping ones.

**Level 1 keeps its 400-node budget deliberately.** In sharp positions it will now examine
only part of the root list and play the best move it got to, which for the level someone
learns on is closer to a feature than a defect: it stays snappy and it gets beatable exactly
where a beginner might find a tactic. It is bounded time and a rougher move, which is the
correct trade — and it is only possible now that a truncated search returns something.

### Phase 5 profile: where search time actually goes, and why the pin set is dropped

**Method — doubling, not sampling.** There is no profiler for a 6502 here, but there does
not need to be one. Run the search once normally, then again with one component doing an
extra redundant copy of its work, and the difference is that component's cost *in situ* —
including the cc65 call overhead a source-level model would miss. Every doubled operation
is either side-effect free (a second `eng_IsAttacked` whose result is discarded, a second
generation into a scratch array) or exactly self-reversing (`eng_Make` immediately followed
by `eng_Unmake`; `scoreMoves` is idempotent and a second `pickBest` finds the element
already in place). **Identical node counts across every run prove the tree was untouched.**
The scratch fork used is not in `src/`; the driver is `tests/c64profile.c`.

Real C64, budget 1600, depth 3, baseline 1303 jiffies. Two independent runs agreed within
5 jiffies on every row:

| Component | Cost | Share |
|-----------|------|-------|
| Move generation | 546 | **41.9%** |
| — of which the 120-square board scan | 91 | 7.0% |
| — of which real generation | 455 | 34.9% |
| make / unmake (incl. the eval delta) | 243 | 18.7% |
| Move ordering (`scoreMoves` + `pickBest`) | 237 | 18.2% |
| Legality (`eng_IsAttacked` after make) | 178 | 13.7% |
| Unaccounted (eval reads, recursion, bookkeeping) | ~100 | ~7.5% |

**Phase 1's central claim is false for the search.** It says: "The hot path is
`eng_IsAttacked`, not move generation. Move generation is only about 8% of perft time."
That was measured on **perft**, where every generated move is made, tested and unmade — so
legality naturally dominates. An alpha-beta search with working move ordering tries only
**2.28 moves per generating node** before a cutoff (measured natively; the search is
deterministic so the counts carry over exactly). Most generated moves are never tested at
all. The ratio therefore inverts: generation 42%, legality 14%.

**So the pin set is dropped.** Legality is 13.7% of search time. A *perfect, free* pin set
that removed every legality call would give 1/(1-0.137) = **1.16x, and that is the ceiling**.
The real thing removes about 89% of the calls — king moves, en passant and genuinely pinned
pieces still need testing — and adds a pin computation of eight rays at every generating
node, against only 2.28 calls to amortise it over. Net is around **7%, or 1.07x**, for the
highest-correctness-risk change left in the plan: the en passant horizontal pin, where two
pawns leave a rank at once, is the classic way these ship a legality bug. That trade is not
worth taking, and it is worth being explicit that the item died on evidence rather than on
effort.

**The general lesson is about where the intuition went wrong**, because it was not a silly
mistake: the reasoning "most moves cannot expose the king, so stop testing them" is correct,
and it is a large win *in a move generator that tests every move* — which is what perft is,
and what `eng_GenLegalMoves` still is. It is a small win in a search that abandons most of
its moves unexamined. **A cost measured on perft does not transfer to the search**, and
every remaining perft-derived assumption in this document should be treated the same way.

**What is left, and why none of it is a multiple.** The profile is flat — 42 / 19 / 18 / 14
with no dominant hotspot, which is what a reasonably optimised program looks like. The
candidates, with honest ceilings:

- **Piece list** instead of the 120-square scan: ceiling 7%, and it grows make/unmake, which
  is already 18.7%. Net perhaps 4-5%.
- **Lazy move scoring**: ordering is 18.2% and `scoreMoves` scores every move when 2.28 get
  tried. Scoring on demand could take a real bite, but `pickBest` still scans the list.
- **Staged generation** in `negamax` (captures first, quiet moves only if no cutoff): reuses
  `eng_GenCaptures` and would be nearly order-preserving. But 83% of generating nodes are
  quiescence and already capture-only, so it only addresses the negamax share.

None of these is a ply. **Phase 5's speed work should be considered done at the 2x already
banked**, and the remaining items are the non-speed ones. If more strength is wanted, the
deferred evaluation terms and a transposition table are better value per byte than grinding
another 5% out of the generator.

### Phase 5 finding: what level 1 actually does in a game

Every C64 timing in this document until now was taken from the **opening position**, on the
stated grounds that it is the conservative case. A user playing the c64 build reported
moves taking about 14 seconds on Very Easy, which did not match the ~11 s the opening
measurement predicted. `tests/c64level1.c` plays a real game at the level 1 setting and
times each move; run twice, and the two runs agree to within a jiffy or two:

| Move | 1w | 2w | 3w | 3b | 4w | 5w | 6w | 7w | 8w | 9w |
|------|----|----|----|----|----|----|----|----|----|-----|
| Seconds | 3.9 | 4.8 | 6.0 | 9.3 | **16.0** | 15.4 | 14.1 | 14.2 | **14.1** | 13.6 |
| Nodes | 152 | 175 | 221 | 300 | **500** | 500 | 500 | 500 | **500** | 500 |

Two things in the plan are wrong, and both come from the same habit of measuring the
opening.

**The opening is the optimistic case, not the conservative one.** The Phase 2 note reasoned
that "a middlegame has fewer pieces and searches faster". After eight moves nobody has
traded — the piece count is unchanged and the lines are now open, so the generator has more
to do and quiescence has more captures to chase. Measured: **45 nodes/sec in the opening
against 35 in the middlegame**. Every budget derived from the opening is about a quarter
optimistic, and every per-level time in section 3 is understated by the same amount.

**Phase 4's "level 1 now completes depth 2 rather than abandoning it" holds only for the
first three moves.** Watch the node column: moves 1-3 finish the iteration inside budget,
and from move 4 onwards the search hits the 500-node cap *mid-iteration*, aborts, throws
that work away and plays its depth-1 move. So in a real game level 1 is still the one-ply
engine Phase 3 described — it just spends 14 seconds being one. That is the worst point on
the curve: the whole budget is consumed and none of it changes the move played.

The general lesson is about the shape of the abort, not about level 1. **A node budget that
lands just short of completing an iteration is strictly worse than one that lands well
short**, because an abandoned iteration is pure cost. When these get re-tuned, set each
level's budget from the node count its depth needs *in a middlegame*, with margin, and
treat "completes the iteration" as the property being bought rather than a time in seconds.

Deliberately not acted on yet. Re-tuning before the pin set lands means doing it twice.

## Phase 5 — closing summary

Every buildable target compiles clean and fits. The c64 at `optspeed`:

| Segment | Before Phase 5 | After | Delta |
|---------|---------------|-------|-------|
| CODE | 24054 | 24739 | +685 |
| RODATA | 2235 | 2235 | — |
| DATA | 344 | 344 | — |
| BSS | 3808 | 3811 | +3 |
| **Total** | **30441** | **31129** | **+688** |

688 bytes bought **2x the search speed** on real hardware, a level 1 that converts won
endgames instead of drawing them, and an engine that can no longer fail to return a move.

Search speed, real C64, NTSC jiffies, opening position:

| Budget | Depth | Phase 4 | Now | Speedup |
|--------|-------|---------|-----|---------|
| 400 | 2 | 1071 | 529 | 2.02x |
| 1600 | 3 | 2456 | 1299 | 1.89x |
| 6000 | 4 | 18095 | 8866 | 2.04x |

Also changed in passing: the AI's status message is `Think` rather than `Thinking`
(`gszThinking` in `globals.c`, read only by `cpu.c`, so no platform file was touched).

**Still open, deliberately.** *Repetition detection* — first noted in Phase 3 and still
absent. It was **not** the cause of the shuffling bug above, which was depth, but nothing
stops the engine repeating a position it has already reached, and at any depth that can
still throw away a won game. It needs incremental position hashing, which is the same
pattern as the incremental evaluation and would also be most of the groundwork for a
transposition table. *(Built in Phase 7, once Part V of `strength.md` put a number on it.)* *The two deferred evaluation terms* remain unbuilt; the note in
`eval.h` explains what each would now cost.

## Phase 6 — Platforms working

- [ ] Build and play-test every target buildable here: c64, c64.chr, apple2, atmos, plus4,
      and the terminal build. A full game each, exercising castling, en passant, promotion,
      undo/redo and all three visualizer toggles.

      **Status: all seven build; two have now been run; the full checklist is still not
      done.** Every target compiles clean and links inside its budget — `atari` and `cx16`
      included, which this item did not expect (see the last item in this phase).

      Run, rather than argued about:

      - **apple2**, under `../a2m-v2` with its control port. Boots, menus, hires board, move
        log, an AI-vs-AI game played through, and the `B` visualizer drawing attack counts.
        Done while verifying the relocated BSS in Phase 7.
      - **plus4**, under VICE's binary monitor — which is how the `cgetc` bug in the Kernal
        key count was found and worked around, a bug that made the menus drive themselves.
        That one is the standing argument against trusting a clean link.
      - the **terminal build**, through AI-vs-AI, the attack overlay, and a human move
        followed by an undo.

      - **atari**, under AltirraSDL's bridge, which is what turned up the framebuffer
        collision in Phase 7 - boot through MyPicoDOS, menus, and an AI-vs-AI game.

      **Still unrun: c64, c64.chr, atmos, cx16.** And no target has been through the
      *whole* checklist — castling, en passant, promotion, undo/redo and all three toggles
      in one sitting. c64, c64.chr and plus4 are drivable headless through VICE's binary
      monitor and apple2 through a2m-v2, so only atmos, atari and cx16 genuinely need
      another machine. This remains the largest gap between what is known and what is
      assumed.

- [X] Update `readme.txt`. **Done, by replacement rather than by editing.** `README.md` is
      now the front page and describes the engine that exists — alpha-beta, quiescence,
      node budgets — while the 2014 text is kept verbatim as `doc/readme-2014.txt`, where
      being out of date is the point: it documents the program that was replaced. The
      original note is left below because it is the inventory of what was wrong.

      **Confirmed stale, more so than the plan assumed.** Section VI
      documents the old algorithm in detail — `gWidth`, `gMaxLevel`, `gDeepThoughts`, the
      stack-ranking of per-piece scores — none of which exists any more; it needs replacing
      with a description of alpha-beta, quiescence, and node budgets. Section II also
      describes the `B`/`A`/`D` displays as "a visual representation of the Attack DB", and
      there is no Attack DB now (the feature is unchanged; the explanation is not). The
      undo stack is described as tracking "the last 254 moves", but `UNDO_STACK_SIZE` in
      `undo.c` is 128, so the real figure is 127. Sections IV and XI's build instructions
      still need re-verifying.

- [ ] Final pass: run the whole test suite, confirm perft is still clean, and record the
      final size, speed and strength numbers against the baselines from Phase 0, Phase 1
      and Phase 5.

- [X] *(On the Windows machine, when convenient.)* Build `atari` and `cx16`. Both had
      pre-existing failures unrelated to this work. If the frozen-interface rule held, no
      engine-side changes should be needed.

      **Neither needed the Windows machine in the end, and the frozen-interface rule did
      hold — no engine-side change was required for either.** `atari` builds here and
      produces a bootable `.atr` via `dir2atr`. `cx16`'s failure was cc65 version drift, as
      suspected: newer cc65 renamed the software stack pointer, so the inline assembly in
      `platCX16.c` wanted `(c_sp)` where it said `(sp)`. Three lines. Both now build on the
      Mac with repetition detection in them; *running* them still needs Altirra and
      x16emu.

---

## Phase 7 — Repetition detection

The last item on the "still open, deliberately" list from Phase 5, and the only one that had
a price attached: 62% of self-play games drawn, all by repetition, 57% of those in positions
the engine itself called winning. Worth roughly a sixth of the score, which is what moved it
ahead of everything else.

- [X] Incremental position hashing in `eng_Make` / `eng_Unmake`, a position history, a draw
      score in `negamax`, and the threefold rule at the board.

**The mechanism is in §6.10 of `engine.md`.** What follows is the part that does not belong in
a reference document.

### The Apple II nearly stopped it before it started

The first design was costed at about 1536 bytes for the key table, and the answer to "does that
fit" turned out to be no — but not on the machine anyone expected. Linking all six targets with
map files and measuring the gap between the top of BSS and each ceiling:

| target | free before | free after |
|---|---|---|
| **apple2** | **460** | 1255 in MAIN, 2128 below HGR |
| atmos | 5700 | 2818 |
| atari | 8438 | 5618 |
| c64.chr | 8542 | 8282 |
| c64 | 13723 | 10841 |
| plus4 | 27488 | 24606 |

The Atari, the machine that prompted the question, had 8.4 KB spare. The Apple II had 460
bytes, and the change needs 2882. It would not have linked.

**The cause was layout, not appetite.** The program starts at `$4000` because HGR page 1 sits
at `$2000-$3FFF`, which strands six kilobytes at `$0800-$1FFF` below the graphics page for the
entire run. The Apple II was also the one target still using cc65's stock config with its
addresses passed as link flags. A project `chessA2.cfg` moves BSS down into that dead space —
note that stock `apple2.cfg` already defines a `LOW` area there, but sizes it `%S - $0800`,
which with a `$4000` start runs straight through the graphics page.

That was verified rather than argued, under `../a2m-v2` with its control port: boots, menus,
board, move log, an AI-vs-AI game, and the B visualizer, with `geBoard` now at `$082D` — the
first 200 bytes of the moved region, which is exactly where a stray ProDOS or loader buffer
would land. A write watchpoint over the unused tail saw no writes in 150 seconds of play, which
is the evidence that the memory is free rather than merely unassigned.

Two things learned while doing it, both of which cost time:

**Reading `geBoard` mid-search shows nonsense.** Twenty-three pieces, impossible positions,
the display mirror disagreeing. It is not corruption — the search makes and unmakes moves on
the real board, so a sample taken while it is thinking is a node from deep in the tree. Only 5
samples in 40 caught the engine idle. Sample until the mirror agrees.

**The loader leaves 31 bytes at `$1E00`** — ascending page numbers `$08..$26`. Inert, never
rewritten, cleared by `zerobss` once BSS grows that far. Noted so that finding them in a memory
dump does not start a hunt.

### The host lied about the cost by nearly half

The equal-time question needs the price of maintaining the hash. Measuring it needs both builds
doing *identical* work, and getting there took three attempts — each time the node counts came
back different, and each time the harness was the reason rather than the engine:

1. the new harness ends games on threefold, so it played shorter games;
2. removing that, the match configuration still switched detection on per move, overriding the
   default the cost build was supposed to be measuring.

Only on the third attempt did both builds report 111,602,938 nodes to the digit, which is the
condition that makes the times comparable at all.

The host then said 5.5%, best of eleven interleaved runs. A real C64, via `tests/c64search.c`
under VICE, said **9.2%, 8.7% and 9.4%** at depths 2, 3 and 4 — identical node counts there
too. A 16-bit XOR and a table index are one instruction on this host and several on a 6502,
while everything they are measured against is comparatively cheaper.

`engine.md` §6.9 already records that *a cost measured on perft does not transfer to a search*.
This is the same mistake wearing a different hat, and it would have credited the change with
about six rating points it had not earned. The equal-time match is charged at the C64's number.

### Result

**+44 Elo at equal nodes, +38 at equal time**, 512 games, 3.7 sigma. Self-play draws 53% → 32%,
decisive games 240 → 350. Size: RODATA +1584, CODE +1038, BSS +260.

### The Atari, which this phase broke

Repetition detection needs 2882 bytes, and the Atari had 1751 spare. That was the wrong
number to be looking at.

`hiresAtari.s` sets `scrn = $9100` and draws a 7680-byte GR.8 framebuffer there, up to
$AEFF. It is an assembler constant. **The linker was never told**, so `ld65` places BSS
wherever MAIN has room, reports a build well inside its budget, and the top of the screen
displays whatever BSS happens to hold. Measured at optsize:

| | static end | against the screen at $9100 |
|---|---|---|
| before repetition detection | $8A29 | clear by 1751 |
| after | $952D | **1069 bytes inside it** |

At optspeed it was already 553 bytes inside before any of this work — which is the real
explanation for a note that had been in the README for years, that the Atari "needs optsize
for the extra kilobyte". It never needed the kilobyte. It needed to end below $9100, and
optsize happened to.

So the sequence is: a latent bug from 2020, a workaround that recorded the wrong cause, and
then a change that pushed the safe configuration over the same edge. Booting the current
build under AltirraSDL's bridge shows it immediately - a band of noise across the top of the
title screen, which is BSS being drawn as pixels.

**The fix is the Apple II's fix again**: find the memory nobody is using. MEMLO is $0700
under MyPicoDOS, and the program starts at $2000. Loading at $0800 crashes - the loader is
still down there while it works - but **$1000 boots, plays, and leaves 2771 bytes clear at
optsize and 723 at optspeed**, so both settings are safe now. Verified through the bridge:
MyPicoDOS boot, menus, an AI-vs-AI game with a clean board.

The cfg also caps MAIN at $9100, which is the part that matters beyond this bug. An overrun
is now a link error rather than a corrupted display.

**The general lesson, and it is not a small one.** Every "all targets link inside their
budgets" in this document is a statement about `ld65`, and `ld65` only knows what the config
tells it. Two of the seven targets keep their framebuffer at an address that lives in
assembly. A clean link proved nothing about either, and on the Atari it was actively
misleading for years.

**Still open.** *Opening variety* — the engine still plays the same first move every game, and
the fix needs an entropy source that `plat.h` does not expose; parked deliberately. *The
transposition table*, which was waiting on exactly the hashing this phase built. *The Stockfish
ladder has not been re-run* since the fix, so every rung in `strength.md` Part IV is a pre-fix
number.

---

## Phase 8 — Finishing won games

Started from a complaint that turned out to be wrong and a measurement that turned out to be
right. The complaint was an Apple II game on HARD where the engine "refused to end" a won
rook ending; the position was in fact a stalemate trap the engine correctly dodged, and the
pieces were being drawn in their opponent's colours, which is why it read as a blunder.

The measurement underneath it stood up. Eighty level-3 self-play games audited against
Stockfish: **12 of 80 drawn with one side at +3 or better**, games running 100 to 344 plies.
And against the pre-repetition build the same audit gave 43 of 80, so repetition detection
had already quartered it - the disease was real and already much improved.

### The instrument came first, and it had been lying

`match endgame` compared `EVAL_ALL` against `EVAL_MATERIAL|EVAL_PST`. Those have been the
same mask since Phase 4 removed the two deferred terms, so it was a configuration playing
itself, returning a perfect 234-234-44 that looked like a result.

W-L-D cannot see this failure anyway: an engine can score dead level and still turn won
endings into draws. So the harness now measures **conversion** - of the sides that were a
clear piece up for ten plies or more, how many won - and splits the failures three ways,
because they want different fixes: drew still a piece up, drew after giving the material
back, or lost. Material is the yardstick, not the engine's own score, because the evaluation
is the thing under test.

Baseline: **69%** from openings, **87%** from endgame positions.

### The king table alone was nearly worthless

Reinstating the deferred endgame king table measured +15 Elo at 1.55 sigma - and moved
conversion **not at all**, 69% before and 69% after. The term it was built to fix did not
respond to it.

The reason is the pawn table. It pays 50 for a pawn on the seventh and 5 for one at home, so
marching a pawn the length of the board earns **45 centipawns, nine a move**, while the
promotion that justifies the march is worth 800 and sits past a depth-4 horizon. In a won
ending the engine had no gradient to climb, so pushing and shuffling scored the same. Giving
the king somewhere to go while the pawns had no reason to move fixed nothing.

### Two tables, one extra total

The fix needed a steep endgame pawn table, and the king-only shortcut could not extend to it:
two kings can be corrected at eval time out of `geKing`, but sixteen pawns cannot - that is
O(pawns) per node, which is the cost that killed these terms the first time.

So carry the **difference**. `geEvalScore` stays the middlegame total it always was, and a
second running total holds only what the endgame tables would add - zero for everything but
pawns and kings. One extra delta per move made, four shift-weighted steps at eval time, and
an immediate exit in the middlegame.

| | equal nodes | equal time, charged the C64's 9% |
|---|---|---|
| king table alone | +15 Elo, 1.55σ | not measured, it had not earned it |
| king and pawn tables | **+44 Elo, 4.41σ** | **+30 Elo, 3.02σ** |

Conversion 69% → **78%** from openings and 87% → **90%** from endgames; games running out the
240-ply limit halved. 1079 bytes, and 9% slower per move on a real C64 - measured with
identical node counts, because `c64search.c` runs from the opening where the blend never
fires, which makes it the honest figure: it is the overhead paid everywhere, including where
the term does nothing.

**This is the first evaluation term in the project to survive the equal-time test.** Pawn
structure and the endgame king table both died there in Phase 4, and the king table died
there again on its own here. What changed is not the measurement standard but the pairing -
the two tables together are worth three times what the better one was worth alone.

### It cost the Atari its optspeed build

1079 bytes was affordable at `optsize` and is not at `optspeed`: the Atari now overflows its
memory area by **562 bytes** and refuses to link, and the Apple II links with **twelve** to
spare. `optsize` remains comfortable - 1742 and 2366 bytes respectively - and is what
`Makefile.options` defaults to.

This is the Phase 7 cap earning its keep rather than a new problem. Before that config existed
the same overflow was silent, and the Atari drew its own BSS across the top of the screen while
`ld65` reported a clean build. A link error is the correct outcome; it is only visible now
because something finally told the linker where the screen was.

Two levers remain unclaimed if `optspeed` is ever wanted back: 1312 bytes on the Atari at
`$AF00-$B41F` between the framebuffer and the stack, and a start address tested only down to
`$1000`.

---

## Phase 9 - the ladder caught up, and the anchor disagreed

Two of the three items left open at the end of Phase 8. The third, opening randomisation, is
still parked and still blocked on the seed rather than on space.

### The ladder, third generation

Sixteen rungs, 512 games each, same fastchess, same Stockfish 18, same book: the conditions of
Appendix A exactly, so the three runs are comparable. **Every rung improved.** The endgame
tables were the only engine change since the §5.1.2 run — everything else that landed was a
platform file, a test or a document — so the delta is theirs:

| Level | mean over four rungs |
|---|---|
| 1 | **+94** |
| 2 | **+77** |
| 3 | **+65** |
| 4 | **+52** |

Monotone in how little the engine searches, which is what knowledge substituting for search
looks like. Level 4 now beats Stockfish at one node by 82 points (248-129-135) where it used
to draw level, and the dead heat has moved two rungs along to 100 nodes.

Two things did not move. The **price of a node** - the first-column spread was 443 points
before both strength changes and is 443 after, so 61 Elo a doubling still holds and the whole
ladder simply lifted. And the **second runner** agrees: c-chess-cli, built for this, reads
-357, -254, -35, +77 against fastchess's -361, -250, -31, +82.

### Self-play was wrong again, in the other direction

§5.1.2's lesson was that self-play overstated repetition detection about threefold. Here it
understated the endgame tables: +44 claimed at equal nodes, +52 to +94 delivered. **The bias
has no fixed sign, so it cannot be corrected with a factor** - which is the useful form of the
lesson, and worth more than either number.

### The anchor says none of it happened

The rated anchor was re-run and, at the only rung measured on both sides of the change, it
disagrees with the ladder flatly: +39, -3, +7, -37 against the ladder's +78, +65, +80, +61.
Some of that is the rung being useless - levels 3 and 4 score 88% against Stockfish's 1320
floor, past the top of the informative band, and the *old* run's own two replicates differed
by 38 points there. The rest may be real: a change that is pure endgame knowledge is worth
more against an opponent that does not search endgames than against one that does, and
node-limited Stockfish at 1 to 300 nodes does not search at all.

So the claim in the document is now the narrow one - worth a great deal against opponents that
do not search endgames, not yet shown to be worth much against opponents that do - and the way
to settle it is a rated rung near 50% measured on *both* sides of the next change. That has to
be set up before the change, not after.

Re-running the anchor properly turned up two harness defects, both of which had been quietly
inside published ratings:

- **A clock-ignoring engine was losing games on time.** fastchess needs some limit declared and
  was handed 30 ms; a level 4 move is ~15 ms here, so host load alone forfeited four games. The
  limit is 5000 ms now, and the node-limited ladder reproduces to the digit across the change.
- **The anchor's answer depends on which rung you read it at**, by 144 points at level 4. A
  200-point step in `UCI_Elo` buys 56 points of played strength up there: Stockfish's limiter
  has run out of ways to be weak that a 4-second clock does not already impose. Each level is
  now played against the two rungs nearest its own strength, and level 4 against Stockfish
  rated 1700 came out 122-121-13, which is as well-placed an anchor point as this project is
  going to get.

The headline ratings move to ~1200 / ~1350 / ~1650 / ~1700, and the ±150 that used to be an
assertion is now a measurement.

### The levels 1 and 2 hypothesis, tested rather than argued

§5.1.2 guessed that repetition detection cost the shallow levels by making the engine decline
draws its evaluation wrongly thought it was better than. `make uci-tuning` now builds the UCI
adapter with the tuning switches exposed as UCI options, so the ladder can be re-run with one
term off against an outside opponent - and with everything on it reproduces the shipped
binary's games to the digit, which is what makes the difference attributable to the switch.

Detection off minus on: level 1 **+15** mean, level 2 **+1**. The mechanism is visible - every
rung's draw count rises with detection off, losses fall to match - and it is worth about a
sixth of what the endgame tables gave the same level. At level 2 the extra draws come out of
the *wins* instead, so the net is zero: level 2 searches deep enough that some of those
repetitions were winnable.

Not a case for removing anything. It retires an open question, which is what it was for.

### The opening randomiser, and a premise that was wrong

This was parked for two phases behind "`plat.h` exposes no clock, so entropy has to come from
human input". Both halves turned out to be wrong, and the second one was wrong in the useful
direction.

**Every one of these machines has a free-running counter, and cc65's own `asminc` names all of
them.** The jiffy clock on the CBM machines, `TIMER3` at $0276 on the Oric, POKEY's `RANDOM`
at $D20A on the Atari. So the entropy was never the problem; nobody had looked. Three of the
six recipes needed correcting on the way through, and the corrections are the instructive part:

- **The SID noise recipe reads the wrong address.** 54272 is $D400, voice-1 frequency low and
  write-only; the oscillator-3 readback is $D41B. It is also the only source of the six that
  *writes* to hardware, which on a port that cannot be run here is the difference between bad
  entropy and a side effect nobody can observe. `TIME` is read-only and was used instead.
- **The Plus/4's jiffy clock is at $A3, not the C64's $A0.** Assuming the C64 address carried
  over would have read something else entirely and looked like it worked.
- **The Apple II has no clock and did not need one.** `$4E/$4F` is a counter cc65's own `cgetc`
  increments while it waits for a key, so it holds how long the human took - better entropy
  than a jiffy counter, not worse. This one was called wrong twice in the same session, first
  as "probably dead" and then confirmed live from cc65's source, which is where it should have
  been checked in the first place.

Whether the clocks are *running* did not need an emulator either. `cgetc` on the CBM machines
and the Oric spins waiting for the IRQ to fill the key buffer - if that IRQ were not running
there would be no way to have reached the code asking the question.

**And the whole "it needs a measurement" worry was misplaced**, which is the better finding.
Alpha-beta returns a move of the same score whatever order the moves are tried in; ordering
decides only among moves that already score *exactly* equal - which is precisely the tie the
old engine was breaking by generator order. Perturbing the ordering score at the root
therefore varies the opening and **cannot** play a worse move. It is a structural property,
not a statistical one, so there was no 512-game match to run.

188 bytes of code and 2 of BSS, and it uncovered a wrinkle in the Atari's budget worth
recording: `DLIST` is page-aligned inside `MAIN`, so 186 of those 188 bytes vanished into the
padding in front of it and the free space fell by two. **50 bytes of padding are left**, and
the byte that exhausts them costs 256 in one step.

### The randomiser reached two moves, so the first move became a table

Running it on real hardware is what exposed the limit. On the Apple II every game opened with a
knight - a *different* knight, which is the randomiser working, but still a knight. Counting on
the host put a number on it: **two distinct first moves, across all 255 seeds, at every skill
level.** Scoring all twenty says why - `b1c3` and `g1f3` tie at 50, `e2e4` and `d2d4` are ten
centipawns behind - and a randomiser confined to exact ties can never reach the third one.

The reflex was to widen it to "within N centipawns", and that reflex was wrong twice over:

- **It would have cost strength for no reason.** A ten centipawn concession to reach e4 treats
  the engine's evaluation as the authority on opening moves, and at 400 nodes it is not. A
  table concedes nothing because it is not asking the search anything.
- **It is not even a constant to change.** `negamax` is fail-hard, so at the root an equal move
  and a *worse* move both return exactly `alpha`; near-ties are invisible without re-searching
  with a lowered window, which costs nodes.

So the first move comes from four hand-picked moves in `cpu.c` - e4, d4, Nf3, c4 - rolled with
the LFSR that was already there. It sits in the game layer rather than the search on purpose:
`cpu_Play` is called by no harness, so the whole measurement apparatus is untouched by
construction rather than by a flag.

**And it measures as an improvement, not a cost.** Each first move got its own 256-position
opening set - same generator and seeds as `book.epd`, differing only in the forced first move -
and 512 games at level 4 against Stockfish at 100 nodes:

| | e2e4 | g1f3 | d2d4 | c2c4 | **b1c3** |
|---|---|---|---|---|---|
| Elo | +1 | −7 | −12 | −19 | **−29** |

All four beat the move the engine chose for itself. The intervals are ±27 to ±30 and overlap
heavily, so no pair is significant alone, but the ordering is opening theory's and the engine's
own preference finishes last. Worth about 20 Elo, and free at the board: no search on move one
is fifteen minutes saved at level 4 on a stock C64.

One thing not done, and it is the honest limit of the above: the sets are four plies deep with
three random plies after the forced move, so this measures positions *derived from* e4 rather
than the e4 line itself. A sharper test would force more of the opening.

### A wrong-way-round table, caught by the test written for it

The first draft of the table encoded the pawns from the wrong end of the board - tile 0 is a8,
so tile 12 is e7 and not e2 - which put *black's* pawns in three of the four entries. Because
the roll is looked up in the generator's output rather than trusted, the effect was not an
illegal move but a silent fallback to searching, and the engine went on playing its two knights
exactly as before. `tests/opening.c` had been written to check the table plays four *different*
legal moves, and it failed with "got 2, wanted 4" on the first run.

That is the whole argument for the lookup and for the test: the failure mode of a hand-written
table is not a crash, it is a feature that quietly does not exist.

### Two things caught in passing

**A green suite can be a stale binary.** `tests/Makefile` did not list the engine headers as
prerequisites, so a mutation test on `search.h` - deliberately breaking the feature to check
that the new test noticed - reported green from a binary that never saw the change. It nearly
became evidence that the test was worthless. Headers are prerequisites now. The related trap
is that `sed -i.bak` and `mv` restore the *original mtime*, which walks straight back into it.

**The eighth port.** `src/c64.chr` is a separate platform file and the build found it the way
these things are always found, by failing to link after the other seven were done.

---

## Phase 10 - a player found what 40,000 games did not

The game was being played on an Apple II and the report was one sentence: *it blunders
checkmate, it doesn't see mate in one on easy or very easy.*

It was true, it was the largest defect in the project, and it had been there the whole time.
Sixty mate-in-one positions from random games, each verified by playing the move and confirming
the opponent is in check with no legal reply: **level 1 solved 27 of them.** Level 2, 54.
Levels 3 and 4, all sixty.

The first instinct was that the opening randomiser had done it, and the first job was to
disprove that: the same probe against the commit before this session's work gives 27 and 54 to
the digit. Pre-existing.

### The cause, and why nothing caught it

`negamax` at depth 0 returns straight into quiescence, before generating anything, so "no legal
move and in check" is never tested. And quiescence stands pat on a static evaluation and looks
only at captures - both illegal when the side to move is in check, because there is no declining
to move and the move that escapes may be quiet. **So a mating move looked like a quiet move, and
mate in one was invisible to a depth 1 search.** Level 1's 400 nodes rarely reach depth 2, which
puts the failure exactly where it was reported. Sorting level 1 by the deepest iteration it
finished settles it:

```
completed depth 1:   4 solved, 32 missed
completed depth 2:  23 solved,  0 missed
```

Two reasons it survived every instrument in `tests/`:

- **The tactics test searched every position with 60,000 nodes** - the level 4 budget. No test
  had ever asked the weak levels a question at the budget they play with.
- **Self-play cannot see it.** Both sides share the blindness, the games come out balanced, and
  the harness reports "ok, balanced" - exactly as it did for the repetition defect in Phase 5.
  That is twice the same shape of miss.

### What the fix was worth

Quiescence now generates evasions rather than captures when in check, does not stand pat, and
returns a mate score when nothing is legal. Mates in one: 27 → **55** at level 1, 54 → **58** at
level 2.

At equal nodes the ladder moved more than any previous change: +44, +82, +47, +63 down the four
levels. But equal nodes is the test this document distrusts, so it was charged properly - and
getting the price took three goes, each wrong in an instructive way.

**First estimate, 30%:** inferred from two match runs that had played *different games*. That is
not a measurement of anything, and it was out by a factor of two and a half.

**Second, 12.2%:** the same work with the flag on and off, on the host. A real measurement, of
the wrong machine.

**Third, 22.7%:** on an emulated C64 with `tests/c64evasion.c`, which replays a *fixed* game so
both builds walk identical positions - 27.6 nodes/sec without evasions and 22.5 with. The host
understated the target by 1.86x, against the 1.64x §5.1.1 found for the position hash. That is
now three times this project has been told that a cost measured on a desktop is not the cost.

Charged at the measured 22.7%:

| Level | budget, on vs off | Elo at equal time |
|---|---|---|
| 1 | 326 v 400 | **+15 / -20** (two rungs; a wash) |
| 2 | 978 v 1,200 | **+82** |
| 3 | 12,225 v 15,000 | **+49** |
| 4 | 48,900 v 60,000 | **+31** |

**Level 1 comes out level, and that is the interesting cell.** Charged its true cost, the depth
it gives up is worth about what the evasions buy. What it gets that the table cannot see is the
ability to finish: 54 of 60 mates in one at the reduced budget against 27 before. A defect fix
is not obliged to be a rating gain.

132 bytes. The Atari crossed its `DLIST` page boundary for the second time in a day and pays 256
for it, leaving 1228 free with 128 bytes of padding.

**And the engine is slower on bare metal than this log has been claiming.** Level 1 now takes
13.2 seconds a move measured over a real game against the 8.2 recorded when the budgets were
set - the accumulated cost of the position hash, the endgame tables and check evasions, about
60% between them. Levels 3 and 4 were never bare-metal settings and are now firmly emulator
territory: level 4 is around three quarters of an hour a move on a stock C64.

### The ladder and the anchor, both moved

Third ladder of the project, and the price of a node still has not changed: the first-column
spread was 443, then 443, and is now 441. Three strength changes have lifted the whole thing
without altering its shape, which is a better argument for the 61-Elo-a-doubling figure than the
original measurement was.

Level 3 now draws level with Stockfish at one node - the dead heat level 4 held one version ago
- and level 4 scores 71% there. The anchor puts the four levels near 1240, 1430, 1700 and 1950,
with the usual warning attached and one new instance of it: every level moved up, so every level
was read against a *stronger* rung than last time, and §4.3.1 shows the implied rating rises with
the rung regardless. Level 4 reading 1946 against 1701 overstates a real gain rather than
measuring it.

### The test that should have existed

`tests/search.c` gained `matein1`, which runs at each level's *own* budget. The positions are
generated and verified rather than invented - the first attempt used hand-written mates and two
of them were not mates, producing a page of failures that meant nothing. Level 1's floor is 10 of
12 rather than 12, because 400 nodes genuinely cannot always finish depth 1 in a sharp position
and a search that never finishes an iteration plays its first ordered move. Turning the fix off
drops it to 5 and 11, so the test bites.

### Still open

*A rated rung near 50%, measured before the next evaluation term lands.* Without it the next
change gets the same three-instruments-three-answers treatment as the endgame tables did.

*A reply book for Black.* The table only fires when the engine opens. Against a human who
plays 1.e4 every time the engine's reply is whatever the root randomiser makes of it, which is
thin for the same reason the first move was. A real reply book needs keying on White's move,
which is a different size of project.

*The Atari has 128 bytes of `DLIST` alignment padding left*, and 1228 free. The byte that
exhausts the padding costs 256 at once. Not a problem, but it is the kind of thing that looks
like one in a link error six months from now.

*The per-node cost of check evasions has never been measured on a C64.* Everything about it in
Phase 10 rests on a host figure scaled by a factor taken from one previous case. `tests/c64search.c`
under VICE is a minute's work for anyone who has VICE installed.

*Level 1 still misses 2 mates in 12, and 5 in 60.* Those are positions where 400 nodes cannot
finish depth 1 at all, so the search plays its first ordered move - a capture. Raising the budget
would fix them and make the level slower; nobody has decided which matters more.

---

## Phase 11 - every draw was a win it did not finish

Started from a different kind of report than Phase 10's. Not a player at a board but a match:
the engine was being played against Sargon II on an Apple II, and at *Very Easy* it was scoring
21% against Sargon's **level 1** - the second-weakest of the seven settings a 1978 program
offers. (Everything measured below is at Easy against the same Sargon level, because that is
where the 64-game baseline was recorded; the twelve-game Very Easy batch is what raised the
question, not what answered it.)

The obvious reading was that the engine is weaker than it claims. It is not what the games say.
Running the material balance over all twelve games of that batch:

**Every single game it drew, it was a clear piece or more up.** Not one draw was a balanced
position. Playing White it reached a clean king and rook against a bare king on move 66, still
had it on move 115, and drew by the fifty-move rule - twice, from two different openings,
because the two are the same game. In another it reached king and queen against king and pawn
on ply 129 and let the pawn promote.

So the score was not a strength problem at all. It was **the entire margin sitting in endings
it had already won.**

### The cause was a table saying the same thing to both kings

`sc_pstKingEnd` sends a king to the middle of the board once the queens are off, which is
right, and it is a *per-piece* table, so it says it to the king being mated as well. The
engine could see that its own king should come out and had no opinion whatever about where the
enemy king should be. With bare kings every rook move therefore scored alike, and it wandered
until the counter ran out.

This is §5.4a's lesson one layer on. The endgame king table alone was nearly worthless because
the pawns had no reason to move; the endgame tables together are nearly worthless in a bare-king
ending because *neither* king has a reason to go anywhere in particular.

### The term could not be a running total, and that turned out not to matter

Every evaluation term since Phase 5 has had to be a property of a piece on a square, because
that is what `eval_MoveDelta` can carry. A mate drive takes **both kings**, so it is not, and
by the rule that has governed this file since Phase 5 it should have been unaffordable.

It is affordable because of *where* it runs rather than what it costs. It sits inside the
`gePhase < PHASE_ENDGAME` test that was already there for the endgame blend, behind a material
gate - so it cannot execute in a middlegame at all, and the positions where it does execute are
ones the search reaches at a few hundred nodes rather than twenty thousand. **The expensive
place and the place this fires are disjoint.** That is the whole argument, and it is the first
time in this project that a term got in by being placed correctly rather than by being cheap.

### The weights are not the textbook ones

The standard formulation weights corner-drive about three times king-proximity - 4.7 and 1.6.
Built that way it was clearly worse than weighting them nearly equally. Five ratios, measured
over the thirteen endings now in `tests/search.c`, counting mates before the fifty-move rule at
each of the four levels:

| corner : proximity | L1 | L2 | L3 | L4 |
|---|---|---|---|---|
| 10 : 2 | 7 | 10 | 13 | 13 |
| 6 : 4 | 10 | 9 | 13 | 13 |
| 10 : 4 | 12 | 12 | 13 | 13 |
| 10 : 6 | 9 | 13 | 13 | 13 |
| **10 : 8** | **12** | **13** | **13** | **13** |

10:8 also gave the fastest mate at every level. **The first guess was that proximity was too
strong** - the queen endings were the ones failing, and a king chasing instead of confining is
what that looks like - so the first experiment halved it. That made level 1 collapse from 12 to
7, which is the opposite of the prediction and settled the question in one run.

The reason is depth. The textbook weights assume a search that can see the corner drive pay
off; at four plies it cannot, and a king that is not already close when the enemy king reaches
the edge spends moves walking there while the counter runs. Proximity is the term that pays
inside the horizon, so this engine wants far more of it than a deep one would.

### What it was worth — measured on the version that turned out to be wrong

Everything in this section and the next was measured before the term met an opponent from
outside this repository, and the two sections after them are what happened when it did. The
figures are kept as they were taken, because which of them survived contact is the whole
lesson. **The authoritative numbers are the table under "The second gate".**

Won endings finished before the fifty-move rule, thirteen positions, the engine defending
itself, `chesstest convert`:

| Level | before | after |
|---|---|---|
| 1 (400 nodes) | 5 of 13, mean 35 plies | **12 of 13, mean 32** |
| 2 (1,200) | 11 of 13, mean 40 | **13 of 13, mean 30** |
| 3 (15,000) | 8 of 13, mean 28 | **13 of 13, mean 23** |
| 4 (60,000) | 13 of 13, mean 20 | **13 of 13, mean 19** |

**Level 3 was worse than level 2 before the change**, 8 against 11, which is worth staring at.
Searching deeper into a flat evaluation finds more equally-scored ways to wander, not fewer.

Against an opponent that actually defends - 100 random won endings, Stockfish holding the weak
side at fixed depth:

| | before | after |
|---|---|---|
| level 1 (400 nodes) | 42 of 100 | **75 of 100** |
| level 2 (1,200) | 61 of 100 | **75 of 100** |

King and queen against a bare king went from 18 of 25 at a mean of 51 plies to **25 of 25 at
17**; king and rook, which is what the Sargon game actually threw away, from 21 of 25 at 48 to
**25 of 25 at 20**. **Level 1 now converts as well as level 2 does**, which is what it looks
like when a term substitutes knowledge for search.

On the project's own instrument, the 512-game sanity match with the same configuration on both
sides, so the only variable is the engine:

| | before | after |
|---|---|---|
| conversion | 338 of 424 (79%) | **364 of 424 (85%)** |
| drew still a piece up | 36 | **14** |
| fifty-move draws | 22 | **12** |
| stalemate draws | 6 | **0** |
| total draws | 126 | **100** |

### Head to head it is level, and Phase 8 already explained why

Still the first version. Played against its own absence, `chesstest match drive`: from the endgame set at 3,000 nodes,
**245-246-21**. From the openings, 221-220-71. Dead level both ways. Only at level 1's budget
does it show in the score at all, 244-232-36, which is +12 games and about half a sigma.

That is not a disappointment, it is Phase 8's own point coming back: *"W-L-D cannot see this
failure anyway - an engine can score dead level and still turn won endings into draws."* The
conversion metric was built for exactly this, and it is the one that moved. The Sargon games
are the same statement from outside: level score, every draw a piece up.

### The first version lost the match it was built to win

Everything above was measured before the fix was played against anything outside this
repository. Against **Sargon II**, the opponent whose games started the phase, it was a
disaster: over 32 games at the same settings as the pre-fix baseline, **51.6% became 31.2%**.

The eight fifty-move draws went to zero exactly as designed. They became *losses*.

The cause was the gate, and it is worth stating plainly because the term itself was never the
problem. The drive was gated on `gePhase < PHASE_ENDGAME` — and `PHASE_ENDGAME` is **3200**,
which is two rooks and two minors still on the board. That is a middlegame. "Walk the enemy
king to the corner" is not weak advice there, it is meaningless advice, and it was being
applied at every node of one.

The divergence was found by diffing one game against its baseline and asking the two binaries
what they thought of the position where they parted, at move 34:

```
pre-fix : score cp -463  bestmove Kc2
post-fix: score cp -561  bestmove b4
```

cc65 was **a piece down** there, at gePhase 1650. The gate `|score| > 400` was written to ask
"am I winning"; it also answers "am I losing", and the term fired for the opponent, correctly
in evaluation terms and disastrously in practice.

### Why every instrument here missed it, and one did not

- `chesstest convert` and the Stockfish endgame benchmark contain **only bare-king endings**,
  every one below gePhase 1000. Neither ever exercised the term outside the range it was
  designed for, so both reported a clean win.
- `match sanity` and `match drive` are **self-play**. Both sides carry the term, the harm
  cancels, and they came out level or better — `match sanity` conversion actually read *higher*
  with the broken gate, 85% against the fixed version's 81%.

**The number that looked best belonged to the version that lost.** That is the third time this
document has recorded self-play flattering a change, and the first time it did so by enough to
have shipped a regression.

### The second gate

`DRIVE_PHASE`, set at 1100, and it is deliberately just above what has been measured rather
than at a guess about what else might benefit:

| | gePhase |
|---|---|
| king and rook, and rook against a pawn | 500 |
| bishop and knight | 660 |
| queen | 900 |
| two rooks | 1000 |
| **the position that caused the regression** | **1650** |

Queen against a lone minor is 1220 and is *not* covered. Nothing here has measured it, and the
lesson of the first version is what a gate set by reasoning rather than by measurement costs.

Everything the term was built for survives the tightening, and the interference does not:

| | pre-fix | gate on score alone | **gate on phase too** |
|---|---|---|---|
| `chesstest convert`, by level | 5/11/8/13 | 12/13/13/13 | **12/13/13/13** |
| Stockfish, 100 won endings, level 1 | 42 | 75 | **75** |
| Stockfish, 100 won endings, level 2 | 61 | 75 | **75**, and faster mates |
| the move at the divergence | `-463 Kc2` | `-561 b4` | **`-463 Kc2`** |
| self-play conversion | 79% | 85% | **81%** |
| self-play fifty-move draws | 22 | 12 | **14** |
| **Sargon II, first 32 games** | **51.6%** | **31.2%** | **57.8%** |

The shipped version was then run to the full 64, against the recorded pre-fix baseline of the
same size and settings:

| Sargon II, 64 games | pre-fix | shipped |
|---|---|---|
| W-L-D | 10W 19L 35D | **27W 20L 17D** |
| score | 42.97% | **55.47%** |
| fifty-move draws | 15 | **2** |
| threefold draws | 18 | 13 |
| checkmates | 29 | 47 |

And on the metric the term was built for — a clear piece up for ten plies or more:

| | pre-fix | shipped |
|---|---|---|
| conversion | 6 of 23 (**26%**) | 26 of 28 (**92%**) |
| drew still a piece up | **15** | **0** |
| gave the material back | 2 | 2 |

**Fifteen games that ended still holding a rook against a bare king, and now none.** That is the
defect the phase opened with, closed against the opponent that exposed it.

By colour, which is where the mechanism is visible rather than inferred:

| | pre-fix | shipped |
|---|---|---|
| **cc65 as White** | 9W 3L 20D — 59.4% | **25W 2L 5D — 85.9%** |
| cc65 as Black | 1W 16L 15D — 26.6% | 2W 18L 12D — 25.0% |

Twenty White draws became five. Black does not move, and should not have: a term that helps
finish won positions does nothing for a side that is rarely winning.

**The colour split is worth more attention than the headline.** Sargon scores 75.0% with the
White pieces and cc65 now scores 85.9% with them, so on equal footing cc65 is ahead - but a
sixty-point gap between colours is not first-move advantage, it is an opening-book gap. cc65
has a book only as White, four entries deep; Sargon has one on both sides. **The remaining half
of the deficit is an opening problem, not an endgame one.**

**And the headline percentage on its own is not significant.** Naive 95% intervals are
30.8-55.1% and 43.3-67.6% and they overlap, over an effective sample of 16 to 17 distinct games
rather than 64. What carries the argument is the conversion count and the fifty-move column,
because those are categorical outcomes tied to the mechanism rather than aggregate noise.

### A caution about this instrument that outlived the measurement

**Sargon is not reproducible.** The harness seeds `$4E` before the level is typed, but the
keyboard-wait counter keeps advancing afterwards, so its book choices depend on host timing.
The same seed opened `1.d4 Nf6` in one run and `1.d4 d5` in the next, with cc65's own moves
identical. A paired replay is therefore impossible and every comparison here is unpaired.

Worse for the arithmetic: **64 games is 17 distinct games**, and Black is four. The fifteen
fifty-move draws in the pre-fix baseline were one game played fifteen times. Scores from this
rig move in large steps and must be read as such - which is exactly why the colour split and
the termination counts above carry the argument rather than the percentage.

### What it costs on a 6502, and the benchmark that had to be built to find out

The host cannot see this term at all. 1,500 searches of 60,000 nodes from endgame positions -
the term firing at every node - came to 6.80s without and 6.63s with, a difference smaller than
the run-to-run spread.

**Neither on-target benchmark could price it either, and the reason is the same trap in two
forms.** `c64search.c` runs from the opening position, where gePhase is 6400 against the term's
bound of 1100: it cannot execute, so that benchmark would have reported zero faithfully and
uselessly. `c64evasion.c` has the right shape - a fixed replay, so both builds walk identical
positions - but the wrong positions, because it replays a middlegame; check evasions cost
nothing where nobody is in check, and this term is the opposite selection problem.

So `tests/c64drive.c`, which is `c64evasion.c`'s shape with the positions inverted. It replays
the pre-fix Sargon baseline game that reached king and rook against a bare king - the defect
itself - and searches only from ply 83, where gePhase first falls to 1100, giving 148 timed
searches of which the term is live in about two thirds. Both halves are the shipping
configuration, built twice, because `EVAL_TUNING` makes every node dearer and a node's cost is
the thing being measured; `eval.h` gained a `-DEVAL_MATEDRIVE_ON=0` override for exactly that,
the same shape `SEARCH_CHECK_EVASION` already had.

On a real C64 under VICE, identical positions in both builds:

| | nodes | jiffies | nodes/sec |
|---|---|---|---|
| mate drive off | 137,323 | 190,616 | 43.225 |
| mate drive on | 139,977 | 198,262 | 42.361 |

**A node is 2.04% dearer.** Against check evasions at 22.7% that is nearly free, and it is the
cost *where the term applies* - in a middlegame it is zero, because the phase test cannot be
entered. Charged at 2%, level 2's 1,200 nodes become 1,176, which at 61 Elo a doubling is about
1.8 Elo.

And charged, it still wins. `chesstest match drive` at equal time, 2,940 nodes against 3,000:
**246-238-28**, where equal nodes gave 245-240-27. **This is the second evaluation term in the
project to survive the equal-time test**, after the endgame tables - and the first to survive it
comfortably rather than by a nose. Pawn structure died there at +2.0σ becoming +0.6σ, and the
endgame king table died there twice.

One practical note that cost twenty minutes. These benchmarks print their results and then spin
so the screen still holds them, so a run lasts until `-limitcycles` fires rather than until the
work is done. The limit has to be computed from the work: 148 level-2 searches need about
3.4e9 cycles, and the 8e9 picked as a round number spent half the run doing nothing.

### Still open

*The Sargon match harness cannot vary the engine's Black games.* Of the twelve games in that
batch there are four distinct ones - the UCI seed is left at zero, so cc65 is fully
deterministic whenever it has Black, and one loss appears four times. The 21% has far wider
error bars than twelve games suggests. That is a harness fix, not an engine one, and it belongs
to whoever is running the match.

*King, bishop and knight against a bare king is 0 of 25 and this does not touch it.* It needs a
table that knows which corner, and the technique runs past thirty moves. Recorded so nobody
mistakes it for something the mate drive should have fixed.

*A check extension.* The Sargon II manual is unexpectedly useful on this: at its **weakest**
setting Sargon examines only its next move *"except where the check is involved"*, and it
*"will automatically search more deeply"* in the opening and the endgame. The engine it is
being measured against has extensions and this one has none. It spends nodes, so it has to be
charged at equal time, and the level 1 cell of Phase 10's table is the warning about what that
can do.

---

## Phase 12 - the colour split was a weighting, and two proxies said otherwise

Started from the last unexplained number in Phase 11: 85.9% with White against 25.0% with
Black. Phase 11 wrote down that this was an opening-book gap, said the remaining deficit was an
opening problem rather than an endgame one, and moved on. That went into `doc/strength.md` as a
conclusion. **It was a guess, and it survived because nobody sorted the games.**

Sorting them took ten minutes. All eighteen Black losses are in two openings. The fourteen
`1.e4 Nc6` games are **identical for all 103 plies**, mate included. Outside those two lines
Black scores 57% and does not lose a game. Twenty-five percent is one loss counted fourteen
times.

Then the claim that the opening caused it, which was equally unchecked. Stockfish over the
losing game: Black is **winning** on move 13 at −181 and throws it on move 17. Sargon's `Nxf7`,
which looks like the refutation of the opening, is worth −139 *to Sargon* — objectively unsound
and practically winning, at 1,200 nodes. And played from `book.epd`, where no book fires for
either side, the engine scores 28.1% with White and 28.1% with Black at level 1. **There is no
colour asymmetry in the engine at all.**

So the deficit is a weighting. White's four-entry table gave 32 games twelve distinct openings;
Black's absence of one gave 32 games five, and the biggest of the five was a loss.

*The journal already knew.* Phase 11's open items contain "The Sargon match harness cannot vary
the engine's Black games... one loss appears four times", filed as a harness fix for whoever
runs the match. The right diagnosis was written down and deferred; the wrong one was written up
and published. Worth remembering which of those two is the more dangerous kind of note.

**Then the reason nobody had caught it: nothing in the repository could play the opening
table.** `tests/uci` calls `search_Best` and never `cpu_Play`; nothing in `tests/` calls
`search_SetSeed`, which the table is gated behind; and `sargon/match.py` had a copy of the
White table written out again in Python because it needed the openings to vary. The feature
shipped for two releases with no instrument able to execute it, and the rig was measuring a
Python replica of what the table was believed to hold. `OwnBook` and `BookSeed` fix that, both
default off so every published game still reproduces.

The Black table is five entries, two replies each, thirty bytes. 253 bytes all in, the third
change in this project to push the Atari's `DLIST` over a page boundary.

### What the rig said, and the part worth keeping

cc65 on Black in every game, three runs. **25.0% to 45.3%** - but the column that matters is
the other one: **five distinct games out of thirty-two became twenty-four out of thirty-two**,
and the worst repeat went from fourteen to five.

```
                          games   W- L- D     score   distinct   worst repeat
no table                     32   2W 18L 12D  25.0%      5 (16%)      14x
table, 1.e4 alt = d6         64  18W 34L 12D  37.5%     26 (41%)      12x
table, 1.e4 alt = e5         32  10W 13L  9D  45.3%     24 (75%)       5x
```

There are three runs and not two because the middle one was half broken, and the per-entry
breakdown is the interesting half:

```
1.e4 d5     15 games   3W  7L  5D   36.7%    11 distinct
1.e4 d6     21 games   0W 21L  0D    0.0%     2 distinct
1.d4 Nf6    12 games  12W  0L  0D  100.0%     1 distinct
```

`1.e4 d6` **rebuilt the exact defect the table was there to remove** - twenty-one games, two of
them, all losses. `1.d4 Nf6` is the same failure winning instead of losing. Only `d5` works,
and the games say why: `2.exd5 Qxd5` forces a capture and a recapture and Sargon diverges from
there, where the closed replies let both programs replay one game.

**Both desktop proxies picked `d6`.** On score it ranked second of eight replies to 1.e4. So a
second instrument was written to measure the property that actually matters - count *distinct
games*, perturbing the opponent's node budget as a stand-in for a real opponent's book - and
`d6` came top of that as well, 13 of 16, the best of any reply. Against Sargon: two.

That is worth more than the score line. §4.3 says a result against one opponent may not
transfer. This says the *variety* a move produces does not transfer either, because variety
against Sargon comes from Sargon's own book and search diverging and nothing on this desk
perturbs those. Two reasonable proxies, both confidently wrong, one of them purpose-built. The
rig is not a confirmation step; on this question it is the only instrument that exists.

`1.e4`'s alternative is now `e5`, picked on the one mechanism the games demonstrated rather
than on either proxy, and re-run:

```
1.e4 Nc6   14 games   0W 14L  0D    0.0%    1 distinct   (before any table)
1.e4 d6    21 games   0W 21L  0D    0.0%    2 distinct
1.e4 e5     5 games   1W  2L  2D   40.0%    5 distinct
```

Five games is not a score and 40% should not be quoted as one. Five games being five games is
the whole point, and the hole is closed.

### Open after this

*The table is nine entries wide against a five-move opponent.* Sargon opened with e4, d4, c4,
Nf3 and f4. A human plays other things, and every one of them falls through to the search and
to whatever single reply it produces. Cheap to extend, and nothing measures it.

*`1.d4 Nf6` wins twelve games out of twelve and they are one game.* Changing it on the variety
argument would mean giving up a 100% entry on the strength of a proxy that has now failed
twice. Left alone deliberately, and flagged so it is not mistaken for an oversight.

*The middlegame is still where the games go.* Both losing lines reached move 13 equal or
better. Nothing in this phase touched that, and it is where the next real strength is.

---

## Phase 13 - both engines at maximum, six games, and a hypothesis that nearly held

Curiosity rather than a measurement: cc65 at Very Hard against Sargon II at level 6, the
deepest of its seven settings, where everything else in this project has played level 1.  Three
games with each colour.

```
cc65 2W 3L 1D (41.7%), 6 distinct openings
8.4 hours, 84 minutes a game, ~45 seconds per Sargon move at max turbo
```

The wall-clock estimate going in was sixty hours.  It was eight, because Sargon's cost tracks
the piece count and endgames are cheap - a useful correction to make out loud, since the
estimate nearly stopped the run.

**Two things six games do settle, because they are existence claims.**  cc65 at Very Hard beats
Sargon at level 6.  And Sargon at level 6 **misses forced mates** - three games, twice in one of
them, where cc65 walked into mate at move 30, was let off, walked into another at move 38 and
was let off again.

The reason is probably the reason this project keeps finding for its own defects.  At 45 seconds
a move under emulation, level 6 on a 1 MHz Apple II was hours a move; it is the setting Sargon's
authors could least afford to play and therefore the least exercised code in the program.  Same
shape as the mate blindness of Phase 10, forty-eight years apart.

**And one thing they do not settle**, which is the part worth writing down.

Games 1 and 2 both turned on an early queen sortie, and game 3 - a win - never moved the queen.
Counting queen moves in the first twenty came out suspiciously clean:

```
losses  4, 4, 4 queen moves      wins  0, 1      draw  2
```

With a mechanism to match: sc_pstQueen is a plain centralization table, d1 scores -5 and the
middle squares +5, so **the evaluation pays the queen ten centipawns to leave home**, and the
punishment in game 1 was a four-move plan against a search that reaches depth 5 or 6.  A
prediction was registered before games 4 to 6 landed, and broadly held.

Then it was measured properly, because correlating with the *result* is worthless - a losing
position invites queen moves as much as the reverse.  Cost per move, every cc65 move of all six
games:

```
queen  25 moves   -70.9 cp/move
rook   24 moves  -134.4 cp/move      twice as expensive
```

**Refuted.**  Except piece type is confounded with phase - rooks barely move before move 15 -
and splitting on it puts the queen back on top where it matters:

```
moves 1-15      queen -85.5,  rook -55.0,  bishop -35.2,  knight -22.1,  pawn -20.1
moves 16-30     rook -145.8,  knight -99.8,  queen -52.3,  bishop -48.2,  pawn -48.2
```

So: in the opening the queen is the most expensive piece cc65 can move, four times a pawn or a
knight, and by the middlegame it is ordinary.  Three different statistics over the same six
games said "the queen", "not the queen, the rook", and "the queen, but only in the opening".
Only the last one controls for anything.

Left as the most promising untested lead in the project rather than acted on, for two reasons.
Fourteen opening queen moves over six games is a small sample.  And Stockfish disliking an early
queen sortie is nearly definitional - it holds opening principles the way a textbook does - so
its agreement is weaker evidence than it looks.  The fix would be free if it works: the table is
64 bytes already in RODATA and retuning it costs no bytes and no cycles on any target.  The test
is an EVAL_TUNING switch, equal time, hundreds of games, and then an outside opponent, because
Phase 12 is where a Stockfish ranking failed to transfer to Sargon on both measures tried.

### Open after this

*The competitive band is unmeasured.*  Everything published is Sargon level 1; this is level 6.
Levels 2 to 5 have never been played, and sargon/README.md already carries the calibration
policy for finding the band.  The gate is how slow each level is, which is a few moves per level
to establish and has not been done.

*Six games cannot support a rate and this one should not be quoted as one.*  41.7% is written
here and in doc/strength.md 5.2a with that warning attached both times.

---

## Phase 14 - principal variation search worked, too little to measure or pay for

The first item from `doc/next-search.md` was principal variation search: search the first legal
move at a node with the full alpha-beta window, give later moves a one-point window, and repeat
the search in full only when the probe says the move can improve alpha.  It had both switches
the work note requires: `geSearchPVS` in the tuning build and `-DSEARCH_PVS=0` in a shipping
build.

The control was exact before measuring the candidate.  With PVS disabled, `uci-tuning` matched
the pre-change shipping binary on all 256 positions in `book.epd` at all four skill levels:
2,048 `info` and `bestmove` records identical, including move, score, completed depth and node
count.  The pre-change Stockfish ladder also reproduced §4.1 to the digit.

Root PVS was not assumed.  Two temporary shipping builds, one applying PVS at the root and one
only below it, were compared over the same 256 positions at every shipped budget:

```text
                 root nodes vs internal-only    depth effect of root PVS
level 1             -4.50%                      9 deeper, 4 shallower
level 2             +0.90%                      identical depths and moves
level 3             -7.95%                     24 deeper, 1 shallower
level 4             -2.73%                     60 deeper, 3 shallower
```

So the root form was the candidate.  Against the full-window baseline its whole-book shape was
the warning the work note anticipated: level 1 used 2.77% more nodes and completed five fewer
plies in aggregate, while levels 3 and 4 used 5.89% and 5.30% fewer nodes and completed 11 and
60 more plies.  A technique designed for deep searches was helping the deep levels and not the
400-node one.

The self-play screens that finished were the two levels the node screen had already predicted
would lose:

```text
PVS level 1 vs full windows    165-237-110
PVS level 2 vs full windows    169-233-110
```

That corroborates the mechanism at levels 1 and 2; it does **not** say PVS lost where it was
supposed to work.  Levels 3 and 4 were deliberately interrupted and never measured by match.
Using self-play to reject at the weak levels is sound here because an independent node screen
predicted the direction.  It would still not be evidence enough to land the technique.

The honest reason to close it is scale.  A 5-6% node saving is about 0.08 doublings, or roughly
5 Elo at the measured 60 Elo per doubling.  The twelve-pairing gauntlet resolves about 18 Elo
at two sigma.  PVS's intended effect is below the noise floor of the instrument that would have
to approve it, so an expensive match cannot turn this into evidence.

It also exposed a portfolio price, not a build defect.  All seven targets compiled and linked,
but on the Atari `DLIST` moved to `$7D00`, BSS ended at `$8F33`, and 460 bytes remained below
the `$9100` framebuffer rather than 716.  The first 62 bytes of growth trigger a 256-byte page
step; once paid, roughly the next 250 bytes are free.  Treating that as a per-technique hard
failure would reject essentially every remaining search candidate for the same reason and miss
the fact that the first one pays for those behind it.

PVS was reverted because a roughly 5 Elo effect is too small to resolve and too small to spend
an Atari page on.  Its low-level loss and deep-level gain are both real parts of that result.

---

## Phase 15 - delta pruning was smaller than PVS

The first quiescence economy got the corrected gate from Phase 14: measure the whole book at
all four shipped budgets before building a target or playing a match.  The implementation was
conservative.  It ran only outside check and above the evaluation's 3200 endgame boundary, and
skipped a move only when stand pat plus the captured material, any immediate promotion gain and
a one-pawn margin still could not reach alpha.

The switch-off control matched the pre-change shipping binary exactly over all 1,024 searches.
With delta pruning enabled:

```text
level   baseline nodes   delta nodes   saving   depth b/c   deeper  shallower
  1          63,224        62,229       1.57%    486/491       5        0
  2         290,234       294,071      -1.32%    516/516       0        0
  3       2,659,235     2,575,266       3.16%    985/996      11        0
  4      13,889,536    13,983,910      -0.68%   1101/1107      6        0
```

The extra completed depths show that the switch was live; the scale closes it.  Its best cell
is 3.16%, far below the roughly 20% needed to resolve an effect in the Stockfish gauntlet, and
the level it was expected to help most gained only 1.57%.  The small node increases at levels 2
and 4 are the budget effect: a cheaper completed iteration can make the next, abandoned one
consume more of the fixed allowance.

Stopped at the pre-gate, before target builds, self-play or external matches.  Delta pruning was
reverted and the next quiescence economy remains a separate experiment.

---

## Phase 16 - the cheap losing-capture test topped out below fourteen percent

The second quiescence economy skipped only the captures that were cheap to call obviously bad:
a more valuable attacker taking a defended, cheaper victim.  It did no SEE, never ran while in
check, and left promotions and equal trades alone.  The extra `eng_IsAttacked` was deliberately
priced later; the pre-gate asks first whether enough nodes disappear for its cycle cost to
matter.

Switch-off again matched all 1,024 baseline searches exactly.  The conservative form measured:

```text
level   baseline nodes   candidate   saving   depth b/c   deeper  shallower
  1          63,224        55,151    12.77%    486/504      18        0
  2         290,234       300,715    -3.61%    516/517       1        0
  3       2,659,235     2,413,098     9.26%    985/1010     25        0
  4      13,889,536    13,879,086     0.08%   1101/1127     28        2
```

That is closer than delta pruning and still below the roughly 20% detectability bar.  The one
permitted dose test also skipped defended equal-value captures.  It barely moved the ceiling:
13.72% at level 1 and 9.84% at level 3, with level 2 still negative and level 4 at 1.36%.
Turning the mechanism up bought about one percentage point, so the conservative test was not
hiding a large saving.

Stopped at the pre-gate and reverted, without target builds or matches.  Full SEE might classify
captures better, but it cannot make this cheap test remove enough additional nodes to justify
its much larger 6502 cost; it remains outside the proposal rather than becoming the next dose.

---

## Phase 17 - null move saved the nodes and not the games

Null-move pruning was the one candidate to clear the new node pre-gate.  The conservative
form gave the side to move a pass only below the root, outside check, never twice in a row,
above the 3200 phase boundary, below a halfmove clock of 99, away from mate bounds, and only
when that side owned a non-pawn piece beyond its king.  It cleared en passant, advanced and
restored the halfmove clock, left the piece hash alone, and suppressed repetition inside the
imaginary line whose side parity cannot describe a legal game.

R=1 and R=2 were measured separately over all 256 book positions at every shipped budget.
R=1 was the useful dose:

```text
level   baseline nodes   R=1 nodes   saving   depth b/c   deeper  shallower
  1          63,224        63,224     0.00%    486/486       0        0
  2         290,234       211,818    27.02%    516/676     160        0
  3       2,659,235         2.582m     2.91%    985/1041     56        0
  4      13,889,536    10,857,708    21.83%   1101/1274    173        0
```

R=2 did not fire at levels 1 and 2 and saved only 7.48% and 9.00% at levels 3 and 4.  That
is the shallow-search hazard from the work note in numbers: the conventional reduction
removed the opportunity before it removed enough work.  The R=1 candidate was therefore
enabled only at the two exact shipped budgets that cleared 20%, level 2's 1,200 nodes and
level 4's 60,000.  With that measured per-level gate, levels 1 and 3 reproduced the baseline
exactly and levels 2 and 4 retained the table above.

The switch-off control was exact over all 1,024 searches.  The full native suite was green,
including the four real skill budgets, and a new temporary state-restoration check exercised
an en-passant position with a nonzero halfmove clock and verified board, hash, running
evaluation, phase, castling, en passant, clock and king squares after the search.  All seven
targets built.  The terminal startup, AI game, human move and attacker/defender display smokes
also passed.  Those candidate-only tests and switches were removed with the implementation.

The Atari number demonstrates why its cliff belongs at the end.  Null move moved `DLIST`
from `$7C00` to `$7E00`, BSS ended at `$9035`, and 202 bytes remained below the `$9100`
framebuffer.  The candidate compiled and linked, but would have paid two page steps.  That is
a large portfolio price; it was recorded, not used to prejudge the strength result.

Self-play provided the required sign and live-switch check:

```text
level 2 null vs baseline   206-198-108
level 4 null vs baseline   203-187-122
```

As usual that is not evidence to land it.  The external gauntlet supplied the result.  Levels
1 and 3, where the budget gate disabled null move, reproduced the baseline ladder exactly.
The two targeted levels pooled over their three Stockfish rungs as follows:

```text
level             W-   L-   D       score       baseline       difference
  2 null        195-1017-324       0.2324         0.2298          +0.0026
  4 null        805- 363-368       0.6439         0.6452          -0.0013
all 12 rungs   1601-3196-1347      0.3702         0.3699          +0.0003
```

From the W-L-D variance, the all-rung difference is about 0.04 sigma; level 2 is +0.20 sigma
and level 4 is -0.09 sigma.  The large node savings are real, but they changed the resulting
moves in ways that were neutral in aggregate: four game-points gained at level 2 and two lost
at level 4 out of 1,536 games each.  This misses the pooled +2 sigma landing gate roughly
fiftyfold.  No Sargon candidate or on-target timing can rescue a technique that has no
measurable strength effect at the preceding gate, so both were stopped there.

Null move was reverted.  It is the useful counterexample to the pre-gate's meaning: 20% is
the minimum scale worth measuring, not a promise that saved nodes become strength.  Here the
screen correctly bought the experiment a gauntlet, and the gauntlet correctly declined to
buy two Atari pages.

---

## Phase 18 - the search portfolio closed empty, with its outside baseline finished

The Sargon level-4 baseline ran beside the desk experiments from its existing sixteen-game
cost probe to the full 64.  It used a frozen pre-experiment UCI binary, the same output
directory and one uninterrupted Apple II bridge, so later rebuilds could not enter it:

```text
cc65 Harder vs Sargon level 4   22W 27L 15D = 46.1%
White                          17W 12L  3D = 57.8%
Black                           5W 15L 12D = 34.4%
28 distinct six-ply openings, worst repeat 6x
49 checkmates, 15 threefolds, 0 fifty-move draws, 0 stalemates
121 mean plies, 4.27 hours
```

That makes level 4 the calibrated outside pairing: the aggregate is inside the 35%-65% band
where a match can move in either direction, and the run has enough variety to read categories
rather than one repeated game.  Black's 34.4% is an existing edge-of-band weakness and twelve
of its games are draws; future confirmations must compare that colour separately rather than
hiding a collapse in the aggregate.  Zero fifty-move draws is the other baseline that matters,
because it says the endgame work still holds against the stronger setting.

The portfolio itself lands no code.  PVS saved 5-6% where it worked, below the instrument's
resolution; delta pruning topped out at 3%; the cheap losing-capture test at 14%; and null move
cleared the node bar at 27% and 22% only to measure 0.04 sigma in the gauntlet.  Every result
was reverted, and the shipping engine finishes this phase unchanged.  The reusable result is
the mandatory whole-book pre-gate, which stopped three too-small candidates cheaply and sent
the one large candidate to the experiment that could show its saved nodes were neutral.

---

## Phase 19 - the transposition table died at the hash-width price

The transposition table was the one remaining search technique not discounted by the failed
portfolio premise. Unlike PVS, delta pruning, losing-capture skipping and null move, a TT can
return work that actually completed rather than save work on an assumption. It therefore got
one price before any table code or RAM-policy decision: widening the existing 16-bit repetition
key to a usable 32-bit transposition key.

The candidate retained the existing 16-bit Zobrist half and formed a second half from a
square-permuted lookup in the same table. That avoided prejudging the separate table-memory
decision by adding another 1,584 bytes of constants, while charging the 6502 for the second
lookup, 32-bit XORs and a doubled repetition ring. `tests/c64search.c` was compiled twice in
the shipping configuration with `cl65 -t c64 -Oris`, and run under VICE in NTSC warp mode.
The compile-time-off control PRG was byte-for-byte identical to the pre-change baseline.

```text
budget   depth   nodes   16-bit jiffies   32-bit jiffies   cost
   400       2     152              289              336   +16.3%
  1600       3    1509             1660             2005   +20.8%
  6000       4    5232            10460            11963   +14.4%
 total             6893            12409            14304   +15.3%
```

The node counts match to the digit at every budget, so both halves timed identical search work.
The candidate PRG grew by 321 bytes even without a second constants table, and the 32-bit
history ring necessarily adds 256 bytes of BSS.

This stops above the work note's 10% on-target human-decision threshold before the TT exists.
The result is large at every measured budget and 15.3% combined, so a table would begin by
making every node dearer on all targets, including the ones with no useful table capacity,
before paying for a probe or store. The memory-size, per-target-chess and visualizer questions
cannot rescue that first cost and were not opened. The pricing scaffold was reverted; the
shipping engine remains unchanged.

---

## Phase 20 - the current C64 profile, retained this time

The Phase 5 profile was the right instrument and an old answer.  It predated the position
history, the endgame and phase totals, mate drive and check evasions, and its modified engine
lived only on a scratch fork.  The first item in `doc/next-engine.md` was therefore to repeat
it on the engine that ships and keep the instrument in the tree.

`tests/c64profile.c` replays six fixed middlegame positions from plies 16 through 21 of the
same level-2 game used by `c64evasion.c`.  Each row performs one redundant copy of one
component.  Baseline and doubled passes run twice in opposite order.  Every pass visits 7,200
nodes; the two rounds therefore require the same 14,400 nodes and the same digest of move,
flags, score, completed depth and node count before a timing is accepted.

The rows are more separated than the old 42 / 19 / 18 / 14 profile:

| # | doubled component | a2m-v2 exact cycles | Apple share | C64 jiffies | C64 share |
|---|---|---:|---:|---:|---:|
| 1 | full move generation | 736,923,964 -> 805,232,556 | 9.27% | 46,453 -> 50,762 | **9.28%** |
| 2 | capture/promotion generation | 736,595,804 -> 953,004,826 | 29.38% | 46,453 -> 60,051 | **29.27%** |
| 3 | scoring | 738,083,164 -> 789,980,667 | 7.03% | 46,453 -> 49,812 | **7.23%** |
| 4 | selection | 737,556,092 -> 773,360,126 | 4.85% | 46,454 -> 48,784 | **5.02%** |
| 5 | ordinary legality | 738,234,364 -> 782,436,583 | 5.99% | 46,454 -> 49,311 | **6.15%** |
| 6 | board-only make/unmake | 737,373,308 -> 772,020,196 | 4.70% | 46,450 -> 48,637 | **4.71%** |
| 7 | middlegame score delta | 737,057,244 -> 804,917,956 | 9.21% | 46,454 -> 50,627 | **8.98%** |
| 8 | endgame delta | 736,404,060 -> 797,938,522 | 8.36% | 46,450 -> 50,186 | **8.04%** |
| 9 | phase delta | 737,702,812 -> 751,999,772 | 1.94% | 46,450 -> 47,370 | **1.98%** |
| 10 | piece-placement hash delta | 739,361,084 -> 799,910,216 | 8.19% | 46,450 -> 50,394 | **8.49%** |
| 11 | `positionKey` and ring write | 739,289,628 -> 741,793,132 | 0.34% | 46,452 -> 46,714 | **0.56%** |
| 12 | repetition scan | 739,726,652 -> 738,714,735 | -0.14% | 46,452 -> 46,525 | **0.16%** |

The a2m-v2 numbers are the development instrument, not a substitute target.  cc65's Apple II
runtime has no `clock()`, so the driver and `tests/profile-run-a2m.py` use a two-byte memory
handshake: the program waits outside the measured interval, the controller pauses, reads the
emulator's exact cycle counter, acknowledges and resumes.  No breakpoint is armed while the
search runs, because a2m-v2 otherwise checks it at every instruction and loses its max-mode
speed.  The Apple and C64 rankings agree closely; the C64 result remains the decision number.

Reproduction, shipping `optsize` throughout:

```sh
cd tests
cl65 -t c64 -Or -I../src -I. -DSEARCH_PROFILE -o /tmp/c64profile.prg \
    ../src/engine.c ../src/eval.c ../src/search.c c64profile.c
./vice-run.sh /tmp/c64profile.prg /tmp/c64profile.png 22000000000

# The Apple binary uses src/apple2/chessA2.cfg, start address $4000 and
# PROFILE_EXTERNAL_CLOCK.  Force-export gc_profileMarker/gc_profileAck, put the
# binary and cc65's loader.system in a copy of apple2/template.po with cadius,
# then use the addresses printed by the map:
python3 -u profile-run-a2m.py /tmp/profile.po --marker 0x1544 --ack 0x1545
```

The full VICE matrix took about 36 minutes on this host; a2m-v2 took about eleven.  Use a2m-v2
to develop and rank profile rows, then use a narrow candidate-specific C64 replay for landing
evidence rather than paying this matrix price repeatedly.

All seven shipping targets were rebuilt with `optsize` and maps after adding the test-only
instrumentation.  Shipping segments are unchanged because every engine hook is behind
`SEARCH_PROFILE`.  The two tight relations remain exactly the roadmap baseline: Atari DATA
ends `$7BC1`, `DLIST` starts `$7C00` (62 bytes of padding), BSS ends `$8E33` (716 bytes below
the `$9100` framebuffer); Apple II MAIN ends `$B271` (1,166 bytes below `$B700`) and BSS ends
`$17B5` (2,122 bytes below `$2000`).

The profile changes the work order.  Quiescence capture generation is again the largest exact
row.  Hash, middlegame and endgame deltas are the next cluster.  History construction and the
repetition scan are below one percent, so B1 is retained as a correctness-priced small screen,
not promoted by the result; B2/B3 matter because they remove the hash and three evaluation
deltas together, not because the ring write is expensive on its own.

---

## Phase 21 - B1, the current ring key is true and not worth reading

The first exact-state screen asked `eng_IsRepetition` to read the newest history entry instead
of reconstructing the current signature from `geHashKey`, castling rights and the en-passant
file.  It is exact under the current history contract.  A tuning-only assertion now checks the
newest entry after a new game, FEN load, actual move, search make/unmake, undo and redo; the game
fuzzer checks it after every probe, move, undo and redo.  Repetition tests and the full suite
stayed green.  Baseline and candidate also returned identical move, score, depth and node
records for all 256 `book.epd` positions at all four shipped budgets: 1,024 searches.

The target price is below the threshold for shipping:

| C64 form | baseline jiffies | doubled repetition jiffies | added cost |
|---|---:|---:|---:|
| reconstruct with `positionKey()` | 46,446 | 46,515 | 69 |
| read newest ring entry | 46,437 | 46,492 | 55 |

Both c64m runs used the retained profiler's six fixed middlegame positions, two rounds in
opposite order, with the same 14,400 nodes and result digest.  The shortcut removes about 14
jiffies from more than 46,000: **0.03%**, while the baselines themselves differ by nine.  The
component saving is measurable; the program saving is not usefully separated from run and
layout noise.

`ENGINE_REPETITION_RING_KEY` retains the candidate for reproduction and defaults off.  Its
compile-time branch costs the shipping build nothing.  The invariant test stays because B2 and
B3 change when history is pushed, and a green repetition suite alone cannot prove the newest
entry still describes the board.  B1 is rejected as a speed change.

---

## Phase 22 - history stops at the quiescence boundary

B2 and B3 share an attractive operation: move the board and running evaluation without
maintaining a position signature no caller will read.  They did not share a result.

The first implementation split make into probe and commit calls.  The fuzzer compared all 128
board bytes, middlegame and endgame totals, phase, piece hash, en-passant, castling, halfmove,
both king trackers and a digest covering the entire history ring plus top and valid around
every legality probe.  It stayed exact, as did all 1,024 `book.epd` searches.  But B2's fixed
C64 result was 47,350 jiffies against 47,324 for the refactored old path: nothing.  Worse, both
were about 1.9% slower than Phase 20's roughly 46,450 because the additional cc65 C calls cost
more than the rejected probes' hash work.  With B3 also active, toggling B2 produced 42,244
against 42,268 in one pair and 42,301 against 42,268 in another.  The sub-tenth-percent result
reversed direction.  B2 is rejected and the ordinary legality path keeps the full `eng_Make`
contract.

B3 owns enough calls to pay.  Quiescence never calls `eng_IsRepetition`; the key and ring may
therefore stay at the parent position throughout that subtree.  The compact implementation
sets one engine history mode at the depth-zero boundary, runs ordinary recursive quiescence
make/unmake with only hash and ring maintenance skipped, and restores the mode on return.  It
does not infer safety from whole-book equality alone.  A test-only quiescence entry snapshots
and compares the board, three totals, key, full ring digest, EP, castling, halfmove and kings
after ordinary completion, budget abort, arena exhaustion and mate.

The final target pair, windowed c64m, NTSC, retained six-position replay in opposite order:

| build | first pass | second pass | nodes / digest |
|---|---:|---:|---|
| compact control, history on | 46,558 | 46,558 | 14,400 / identical |
| B3 compact boundary mode | 42,234 | 42,235 | 14,400 / identical |

That is **9.3% less C64 time at the same nodes**.  Baseline and B3 also returned identical
move, score, depth and node records for all 256 opening positions at every shipped budget.

The first split API grew common code by 259 bytes and pushed Atari's display list from `$7C00`
to `$7D00`, spending a 256-byte page for no chess.  Passing a history flag to every make/unmake
cut the growth to 102 bytes but still crossed the page.  Setting the mode once at the
quiescence boundary is 46 bytes over Phase 20: Atari DATA ends `$7BEF`, leaving 16 bytes before
the `$7C00` display list and the full 716 bytes below the framebuffer; Apple II MAIN ends
`$B29F`, leaving 1,120 bytes, and its BSS still ends `$17B5`, leaving 2,122.  All seven targets
build at `optsize`.

This saving is banked as faster fixed-budget search only.  No node budget changes here.

---

## Phase 23 - restore search totals instead of calculating them twice

B4 prices the other half of the running-state work.  Make still computes the piece hash,
middlegame score, endgame score and phase once.  Search records their old values before making
a move; unmake restores the board, king, en-passant, castling, halfmove and history top, skips
the four inverse delta calls, and search copies the old totals back.

The state is separate from the 128-entry user undo ring: one 8-byte record at each of the twelve
reachable move-making plies, 96 bytes.  Ordinary undo, redo, board legality and UI callers keep
the original delta-based `eng_Unmake`.  A search-only mode selects restore behavior once around
the search rather than adding an argument to every hot call.

The fuzzer runs every chosen random move through both slow and fast unmake from the same parent.
It compares the undo record, all board bytes, kings, EP, castling, halfmove and the full ring
digest; it then restores the four saved totals exactly as search does.  Promotions, en passant
and castling are deliberately preferred.  The ordinary suite stayed green, and B4 off/on
returned identical move, score, completed depth and node records for all 1,024 whole-book
searches.

Paired windowed c64m result on top of B3, same retained middlegame replay:

| build | first pass | second pass | nodes / digest |
|---|---:|---:|---|
| B3, delta-based unmake | 42,282 | 42,283 | 14,400 / identical |
| B3+B4, restored totals | 38,557 | 38,556 | 14,400 / identical |

B4 removes **8.8%** of the remaining C64 time.  From the exact compact history-on control at
46,558 jiffies, B3+B4 together remove **17.2%** at the same fixed budget.

Memory layout chose between two correct implementations.  Four parallel per-ply arrays ran at
38,235 jiffies, another 0.8 percentage points, but their repeated cc65 indexing code moved the
Atari display list to `$7E00` and left 107 bytes below the framebuffer.  One array of 8-byte
records runs at 38,557 and keeps `DLIST` at `$7D00`: DATA ends `$7CF7`, BSS ends `$8F94`, and
363 bytes remain below `$9100`.  Apple II MAIN ends `$B3A7`, leaving 856 bytes; BSS ends `$1816`,
leaving 2,025.  The 256-byte Atari page is worth more than 0.8 percentage points of an already
faster move.

The final `chess.po` also booted in windowed a2m-v2, advanced through the menus, searched at the
default skill and played White's `e2-e4`; the rendered board and move log both updated.

No budget changed.  This is banked fixed-budget time for Phase D.

---

## Phase 24 - B5 saves cycles and costs the wrong page

After B4, phase and endgame deltas run only on make in the search.  B5 first guarded them from
facts already decoded there: phase can change only on a non-pawn capture or promotion; the
endgame difference can change only when a pawn or king moves or is captured.  The middlegame
delta remained unconditional.

The direct guards passed the full suite and returned all 1,024 whole-book records identically.
On the retained C64 replay they were a real but small win:

| build | first pass | second pass | change |
|---|---:|---:|---:|
| B3+B4 control | 38,557 | 38,556 | — |
| direct phase/end guards | 37,662 | 37,663 | **2.3%** |

They grew common code by 71 bytes.  B4 had only eight bytes of padding left before Atari's
next display-list page, so `DLIST` moved from `$7D00` to `$7E00`, BSS ended `$9094`, and only
107 bytes remained below the `$9100` framebuffer.  The code itself fits; the page it triggers
is the unacceptable part.

The alternate dose fused all three running updates behind one call, retaining the pure delta
functions as references.  It saved two bytes relative to the direct guards—nowhere near the 63
needed to recover the page—and the extra cc65 call reduced the C64 win to 38,558 -> 38,297,
**0.7%**.  That settles both forms: the mechanism has a measurable ceiling, and it is not worth
a 256-byte Atari page.  B5 was reverted completely.

---

## Phase 25 - the remaining hash path is below the assembly threshold

B6 had been ordered after the earlier exact-state changes specifically so assembly would be
written only for work still present in the resulting hot path.  B3 and B4 changed that path
more decisively than the old profile implied: quiescence no longer maintains a key, and search
unmake restores its old key rather than calculating the inverse delta.

The retained profiler was therefore run again on the B4 shipping tree, one row at a time.  Both
runs used the six fixed middlegame positions, opposite pass order, 14,400 nodes and the same
result digest:

| doubled component | baseline | doubled | remaining share |
|---|---:|---:|---:|
| `hashDelta` | 38,557 | 38,724 | **0.43%** |
| history construction | 38,556 | 38,584 | **0.07%** |

The second row includes `positionKey` and the ring write.  Thus a replacement that made every
remaining key and history operation free could remove only about **0.51%** of this replay.  A
real ca65 lookup or alternate accumulator cannot make them free, and B4 has only eight bytes of
padding before another 256-byte Atari display-list jump.  B6 was closed without writing an
assembly module.

B7 can make a separate size argument: a square table plus rotations or two byte accumulators
might replace the 1,536-byte Zobrist table.  It would also replace a signature whose collision
behavior is intentionally random with one that needs a million-position distribution and
false-match campaign before it is safe.  The shipping B4 tree has 363 bytes below Atari's real
framebuffer ceiling, and no accepted Phase B change is waiting on the table space.  With a
combined speed ceiling of half a percent, that correctness campaign has no current
prerequisite result to justify it.  B7 was not prototyped and the existing signature stays.

Phase B ends with B3 and B4.  Together they reduce the exact fixed C64 replay from 46,558 to
38,557 jiffies, **17.2%**, and preserve all 1,024 whole-book results.  B1, B2, B5, B6 and B7
are rejected on their recorded screens.  A final forced native suite passed, including two
fuzz sets totalling 300 games, depth-five perft, tactics, every shipped skill budget and the
512-game sanity match.  All seven targets then rebuilt from scratch at `optsize`.  The final Atari map
keeps `DLIST` at `$7D00`, DATA ending `$7CF7`, and 363 bytes below `$9100`; Apple II leaves 856
bytes in MAIN and 2,025 in BSS.  The handoff branch is
`codex/next-engine-phase-b-exact-state`; no Phase C branch has been created.

---

## Phase 26 - fuse scoring with first selection (C1)

C1 tries to remove the first `pickBest` scan by remembering the best score during `scoreMoves`
and leaving that move at index 0 for internal negamax and quiescence lists.

The root cannot share that path.  Random opening perturbation and previous-iteration priority
rewrite scores after the scoring pass.  Placing a first move before those rewrites changes the
later selection-sort order among equal scores: one book position at level 3 completed a deeper
iteration and changed its best move.  Root therefore keeps plain scoring and a full first
`pickBest`; only internal lists fuse.

With that split, switch-off equivalence holds: all 1,024 whole-book records are identical at
every shipped budget, and a native test compares the full fused selection order against classic
score-then-pickBest on full and capture lists, including a planted killer.

Two implementations were timed on the retained six-position C64 middlegame, windowed c64m,
identical 14,400 nodes and digest:

| form | first pass | second pass | vs control |
|---|---:|---:|---:|
| control (`SEARCH_SCORE_FIRST=0`) | 38,557 | 38,556 | — |
| size-conscious `placeFirst` flag | 38,589 | 38,590 | **+0.08%** (slower) |
| duplicated `scoreMovesSelectFirst` | 38,418 | 38,418 | **−0.36%** |

The flag form costs tracking and a runtime place check on every scored move and loses more than
the skipped first scan returns.  The duplicated body wins a third of a percent but grows Atari
search CODE by enough to push `DLIST` from `$7D00` to `$7E00` (210 bytes of CODE growth in the
flag form alone; the duplicate overflows far worse).  After B4 only eight bytes of padding remain
before that page.  Same verdict as B5: a sub-percent exact win is not worth a 256-byte Atari
page.

`SEARCH_SCORE_FIRST` retains the candidate, default off.  Shipping maps again match Phase B:
Atari DATA ends `$7CF7`, `DLIST` at `$7D00`, BSS ends `$8F94`.  C1 is rejected.

---

## Phase 27 - on-demand legality (C2)

C2 is the per-move form of the pin idea Phase 5 rejected: after a make, skip the full attack
walk when the move cannot have discovered a check.  King moves, en passant and positions that
were already in check still use `eng_IsAttacked`.  Unaligned origins are legal without a walk.
Aligned origins need either a vacated-ray scan or the full walk.

`eng_LeavesInCheck(side, move, wasInCheck)` is the entry.  Search, `eng_GenLegalMoves` and the
UI legal-move list all use it when `ENGINE_FAST_LEGAL` is on.  Correctness held: depth-five
perft, 300 fuzz games, tactics, mate-in-one at every real budget, and all 1,024 whole-book
records are identical to the full path.  Constructed tests cover horizontal en passant
discovery, orthogonal and diagonal pins, sliding along a pin, capturing the pinner,
aligned-but-blocked free pieces, interpositions while checked and double check.  The native
suite builds with `-DENGINE_FAST_LEGAL=1` so those gates cannot go stale.

Speed on the retained C64 middlegame, slim form (unaligned early-out only):

| build | first pass | second pass | nodes |
|---|---:|---:|---:|
| control (shipping) | 38,557 | — | 14,400 |
| slim `ENGINE_FAST_LEGAL=1` | 37,665 | 37,671 | 14,400 |

That is **2.3%**.  The full vacated-ray form was not re-timed after it overflowed Atari.

Size killed both forms before the speed number could matter:

| form | engine CODE delta | Atari |
|---|---:|---|
| vacated-ray scan | +694 | MAIN overflow **661** bytes |
| unaligned early-out only | +293 | MAIN overflow **149** bytes |

After B4 the Atari has 363 bytes below the framebuffer and eight bytes before the next
`DLIST` page.  Neither C2 form fits.  The 2.3% is the same figure that rejected B5 for a page;
here the binary does not even link.  `ENGINE_FAST_LEGAL` defaults off, call sites compile back
to direct `eng_IsAttacked` when off, and shipping maps match Phase B again.  C2 is rejected.

---

## Phase 28 - dedicated capture generator (C3)

Capture generation is the largest Phase 20 row (29%).  C3 replaces the shared
`sc_capturesOnly` walk with a dedicated write-pointer generator: no quiet empties are
written, promotion pushes are kept, castling is omitted, and the board/piece order matches
`eng_GenMoves` so the quiescence subsequence test still holds.

Correctness: the existing `qgen` walk over the perft trees stayed green, and all 1,024
whole-book records matched the shared-path control.  The native suite forces
`-DENGINE_DEDICATED_CAPTURES=1`.

C64 fixed middlegame, windowed c64m:

| build | first pass | second pass | nodes |
|---|---:|---:|---:|
| shared `sc_capturesOnly` | 38,557 | 38,556 | 14,400 |
| dedicated write-pointer | 36,145 | 36,149 | 14,400 |

**6.3%** — the largest Phase C speed result so far.

Atari size killed it before any ca65 follow-up: engine CODE 6,252 -> 7,340 (+1,088), MAIN
overflow **921 bytes**.  The tight target has 363 bytes below the framebuffer after B4.  A
common assembly slider kernel cannot recover a kilobyte of duplicated C control structure.
`ENGINE_DEDICATED_CAPTURES` defaults off.  C3 is rejected; reopen only if §10 or another
change frees on the order of a kilobyte on Atari.

---

## Phase 29 - zero page and piece lists (C4, C5)

C4 requires every target map before placing hot scalars in zero page.  The shipping
`ZEROPAGE` segment is **fully used** at `$1A` bytes on c64, c64.chr, plus4, cx16, atmos and
atari's consumed portion; Apple II's cfg only offers `$1A` total.  Atari configures `$7E`
bytes from `$82` but only the same `$1A` runtime block is filled — the spare is Atari-only
and not available as a common engine placement.  No zero-page move of generator pointers or
search scalars was attempted.

C5 stays deferred.  Phase 20 does not isolate a board-scan cost that outranks the already
measured capture-generator path, and piece lists tax every make/unmake.  Not implemented.

**Phase C is complete.**  C1–C3 were exact, measured and rejected (noise or Atari size).  C4
and C5 are closed as deferred screens with map evidence.  Shipping remains the Phase B engine
(B3+B4) at 38,557 jiffies on the fixed C64 middlegame.  The largest Phase C speed signal that
did not fit is C3 at 6.3%.  Hand-off branch: `codex/next-engine-phase-c-exact-work`.

---

## Phase 30 - incremental pawn structure (E1)

Phase 4's doubled/isolated/passed bundle scored +2.0σ at equal nodes and +0.6σ at equal time
after a full-board leaf evaluation made every C64 node 1.35x dearer.  E1 rebuilds doubled and
isolated only (passed stays separate), with dual switch `EVAL_PAWNSTRUCT` / `EVAL_PAWNSTRUCT_ON`.

### Size path

An incremental make/unmake form with per-file counts cost ~750 CODE bytes on Atari and pushed
`DLIST` past the framebuffer.  A board-scan form at `eval_Position` time fitted after a
hand-coded 6502 walk in `src/pawnstruct.s` (~248 CODE, 23 BSS when on): Atari `DLIST` `$7E00`,
**82 bytes** below `$9100`.  Host C reference agrees with the asm on constructed positions
under c64m (start 0, white doubled −8, white isolated −16, black doubled+isolated h +40).
Doses are powers of two (−8 doubled, −16 isolated).

### Strength screens

| screen | result |
|---|---|
| purpose tests + fuzzer + suite | green with `EVAL_PAWNSTRUCT_ON=1` forced in the native suite |
| switch-off vs Phase C tip | all 1,024 whole-book searches identical |
| equal nodes (512 games) | **228-208-76** (~+0.9σ) structure vs none |
| equal time, host 38% charge (1450 vs 2000) | **182-244-86** (~−2.5σ) |
| host nps (64 skill-3 book positions) | on 4.59M / off 6.35M → **~1.38x** dearer a node |
| SF gauntlet absolute (ON, 512/pairing) | levels 3–4 within noise of OFF subsample |

The equal-time loss is Phase 4's failure mode again: the scan buys a weak equal-node edge and
pays more than it is worth once nodes are charged.  Incremental maintenance would remove the
eval-time walk but does not fit Atari without a separate memory reclaim (undo pack / arena).

### Shipping

`EVAL_PAWNSTRUCT_ON` defaults **0**; `EVAL_PAWNSTRUCT` is not in `EVAL_ALL`.  The candidate and
asm stay in tree for reproduction; the native suite forces the compile switch on so the gates
cannot go stale.  Shipping maps match Phase B again (Atari `DLIST` `$7D00`, 363 free).

**E1 rejected** for equal-time cost.  Branch: `codex/next-engine-phase-e1-pawn-structure`.

---

## Phase 31 - KBN vs bare king (E4)

Named defect: king+bishop+knight vs bare king was 0/25 and mateDrive pushes to any corner.
E4 adds `kbnDrive`: fire only when `gePhase == 650` and the winner has exactly B+N with the
loser bare; drive the losing king to bishop-colour corners (a8/h1 light, h8/a1 dark) and
bring the winning king close.

Host result with a full-weight form: level 4 converted 8/8 KBN positions (mean ~73 plies) at
the real 60,000-node budget; levels 1–3 converted 0/8.  Atari could not fund that form
(~+1.2 KB eval CODE).  A size-slim form still overflowed (~+680 CODE) and only 4/8 at level 4.

`EVAL_KBN_ON` defaults 0; bit not in `EVAL_ALL`.  Native suite forces ON with floors
0/0/0/1.  Shipping maps unchanged (Atari `DLIST` `$7D00`, 363 free).

**E4 rejected** for size.  Branch: `codex/next-engine-phase-e4-kbn`.

---

## Phase 32 - bounded check extension (E5)

One extra ply when the side to move is in check: `nextDepth = depth` instead of `depth-1`,
capped by `SEARCH_MAX_PLY`.  Code is small but Atari still takes a `DLIST` page (363 → 107 free).

Whole-book at fixed budgets: L1 −2.5% nodes with less completed depth and 2 move changes; L3
similar; not a free win.  Mate-in-one at level 1's real budget fell **11/12 → 10/12**, below
the suite floor.  That is enough to stop.

`SEARCH_CHECK_EXT` defaults 0.  Candidate retained for reproduction.

**E5 rejected.**  Branch: `codex/next-engine-phase-e5-check-ext`.

---

## Phase 33 - mine failures; E2/E3 status (E6)

`tests/mine_failures.py` implements the E6 gate: first durable Stockfish swing on our losses,
mate scores dropped, tagged by mover kind.  Sample L3 vs SF 30 nodes, 64 games: 24 losses,
queen moves lead the first-swing count (7/23).  That steers E2/E3 toward queen/development
work rather than new king-safety.

E2 (locked train/val value tuning) and E3 (queen-before-minors interaction) were **not** run
as full campaigns in this pass: E1/E4/E5 closed empty and E6's job is to choose the next
hypothesis, not invent one from a single sample.  Both remain open with E6 as input.

**E portfolio (this pass):** E1 rejected equal-time; E4 rejected size; E5 rejected L1 mates;
E6 instrument + sample; E2/E3 deferred with direction.  Shipping still Phase B speed (B3+B4).

Branch tip: `codex/next-engine-phase-e6-mine` (stack includes E1/E4/E5).  Merged to
`master` at `da38df2` before Phase D.

---

## Phase 34 - spend B3+B4 (Phase D)

The only exact-speed survivors were B3+B4 (−17.2% on the fixed C64 middlegame). Nothing from
C to combine. The product split:

- Very Easy 400 and Easy 1,200 stay. Those levels are menu experience; 400 is a conversion
  floor. Whole-book at both budgets is **identical** (0 move, 0 depth changes).
- Harder 15,000 → **18,000**. 15/256 book positions complete another ply, 5 change the move.
  49 same-depth positions spent extra nodes (started a ply they then aborted). Not cosmetic.
- Very Hard 60,000 → **65,000**, short of the 16-bit edge. 23/256 complete another ply, 5
  change the move.
- Depth caps stay 3 / 4 / 5 / 6. Same budgets on every target.

C64 `c64skill.c`, 20-ply self-chosen game, NTSC jiffies / 60:

| level | mean | lo | hi | mean nodes | full depth |
|---|---:|---:|---:|---:|---|
| 1 | 659 (11.0s) | 267 | 1018 | 307 | 0/20 |
| 2 | 2383 (39.7s) | 1230 | 3464 | 1115 | 0/20 |

That is the published 13s / 46s after B3+B4, banked rather than spent.

`c64phased.c` — same B3+B4 engine, old vs new L3/L4 budgets, jiffies:

| pos | L3 15k | L3 18k | L4 60k | L4 65k |
|---|---|---|---|---|
| opening | 5292n / 9532t / d4 | 18000n / 25036t / d4 | 43872n / 48077t / d5 | identical |
| middlegame | 11353n / 20893t / d3 | identical | 60000n / 158796t / d3 | 65000n / 172780t / d3 |
| KRK | 15000n / 14637t / d4 | 16298n / 15834t / **d5** | 31515n / 42209t / d6 | identical |

Middlegame L4 at 65k is 48.0 min vs 44.1 min at 60k (same engine). Opening L3 spent the extra
3,000 nodes at the same depth. The extra ply shows up in the ending, which is where it was
meant to.

Mate-in-one at the real budgets: 10 / 12 / 12 / 12, floors held. Convert: L3 13/13 mean 22
plies (was 23), L4 13/13 mean 21 (was 19).

Stockfish ladder, 512 games, L3 and L4, **same-day** `/tmp/uci-d-baseline` (15k/60k) against
`tests/uci` (18k/65k). fastchess alpha 1.8.2, Stockfish 18, `book.epd`. A re-run of L1/L2
did not reproduce Appendix A to the digit (tool drift); those rows were left alone.

| level | SF | 15k/60k today | 18k/65k | delta |
|---|---:|---:|---:|---:|
| 3 | 1 | +19 | **+44** | +25 |
| 3 | 30 | +12 | **+33** | +21 |
| 3 | 100 | −74 | −51 | +23 |
| 3 | 300 | −217 | −205 | +12 |
| 4 | 1 | +160 | +150 | −10 |
| 4 | 30 | +145 | +142 | −3 |
| 4 | 100 | +16 | +16 | 0 |
| 4 | 300 | −112 | −120 | −8 |

Harder is a real granted-node gain, a bit above the 16 Elo formula. Very Hard is noise.

Sargon II L4, 64 games, cc65 skill 3 at 18,000 nodes, windowed a2m-v2, OwnBook on:

**22W-13L-29D = 57.0%**, 33 distinct six-ply openings, 0 fifty-move draws.
Terminations: 35 checkmate, 22 threefold, 7 insufficient material. Mean 115 plies,
max 245 (two long White conversions that finished). Colour: White 11-9-12, Black
11-4-17. One parse timeout (`G5F5` missing hyphen) retried cleanly.

Against the previous 15,000-node pairing (22-27-15, 46.1%, 28 openings, Black
5-15-12): same win count, fourteen fewer losses, more draws, more openings, and
the weak Black side is gone. No categorical regression. Score's honest interval
at 64 games is ±12 points; 57% vs 46% sits on that edge and is read as
confirmation, not a new rating.

`scratch/sargon-l4-phase-d-20260812`. Branch: `codex/next-engine-phase-d-budgets`.

---

## Decisions on record

Kept here so they do not get relitigated.

- The reference machine for all timing is real 1 MHz hardware at 1x. Emulator acceleration
  is a free multiplier and is not designed for.
- **Every target is built at the same optimisation setting, and it is `optsize`.** A port
  built differently is a port that behaves differently, so the setting is uniform rather
  than per-target. Since Phase 8 it is also the only one that fits: `optspeed` overflows
  the Atari by 562 bytes and leaves the Apple II twelve. Not revisited without a reason
  better than free speed on the targets that could take it.
- Budgets are node counts, never wall-clock, because the platforms share no timer and
  determinism across ports is worth more than adaptive timing.
- **A rated-anchor rung is only read where the engine scores near 50%.** Levels 3 and 4 score
  88% against Stockfish's 1320 floor, and a rung like that cannot register a 60-point change -
  which is how Phase 9's largest gain came to be invisible to it. Each level gets the two rungs
  nearest its own strength, and the anchor runs below the host's core count because it is the
  one measurement with a clock in it.
- **No published figure comes from the tuning build.** `uci-tuning` exists for A/B work and
  must be shown to reproduce `uci`'s games to the digit before its differences mean anything.
- `plat.h` and 0-63 tile numbering are frozen so that the untestable ports stay safe.
- The opening book was revisited and shipped in Phase 12. The transposition table was rejected
  in Phase 19 at its prerequisite hash-width price: 15.3% on a C64 before probe or store.
- The current engine's speed problem was largely self-inflicted by the data structure
  rather than by C: legality testing cost a full Attack DB rebuild. Once that is a
  ray-cast, accurate and fast stop being opposed, which is why `gDeepThoughts` can go.
