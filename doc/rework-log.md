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

### Still open

*Opening randomisation.* Unchanged from Phase 8: the engine plays the same first move every
game, there is room for the term at `optsize`, and `plat.h` exposes no clock, so entropy has to
come from human input and the first move after a cold boot stays deterministic whatever is
done. This one needs a decision before it needs code.

*A rated rung near 50%, measured before the next evaluation term lands.* Without it the next
change gets the same three-instruments-three-answers treatment as this one.

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
- The opening book and the transposition table are deferred, not rejected. Revisit after
  Phase 4.
- The current engine's speed problem was largely self-inflicted by the data structure
  rather than by C: legality testing cost a full Attack DB rebuild. Once that is a
  ray-cast, accurate and fast stop being opposed, which is why `gDeepThoughts` can go.
