# How the cc65 Chess engine works

A guide to the engine behind this program: what it stores, how it thinks, and why each
piece of it is shaped the way it is. It assumes you know how the pieces move and nothing
else about computer chess.

It is written to be read once, front to back, after which the source should be navigable.
Every idea is tied to the function and file that implements it.

---

## Executive overview

A chess engine has to answer one question: *of the moves I can play, which is best?*

Everything else follows from taking that question seriously. To answer it you must be able
to (a) hold a position, (b) list the legal moves from it, (c) play a move and take it back
again, (d) put a number on how good a position is, and (e) look ahead — try a move, see the
opponent's best reply, and the reply to that, far enough that the number at the end means
something.

This engine does those five things in five pieces:

| Concern | File | The idea in one line |
|---|---|---|
| Position | `engine.c` | A 128-byte board where "off the board" is a single AND. |
| Moves | `engine.c` | Generate everything a piece could do, then filter by trying it. |
| Play / take back | `engine.c` | `eng_Make` / `eng_Unmake` on a 4-byte per-move record. |
| Judgement | `eval.c` | Material plus piece-square tables, kept as a running total. |
| Looking ahead | `search.c` | Alpha-beta negamax with move ordering and quiescence. |

Two decisions shape everything above, and both are worth understanding before the details.

**The evaluation returns a *position value*, not a *move preference*.** This sounds like
hair-splitting and is not. The engine this replaced scored how much it *wanted to play a
move* — "capturing here looks good, +40". Such a number cannot be compared across plies or
negated for the opponent, because it is not a property of anything; it is an opinion about
an action. A position value can be: if a position is worth +150 to White it is worth −150
to Black, by definition, and that symmetry is the entire mechanism that lets a search pass
scores up a tree. Get this wrong and no amount of search depth helps, because the numbers
being maximised do not mean anything. See the header comment in `eval.h`.

**Attacks are computed on demand, not stored.** "Is this square attacked by Black?" is the
most frequently asked question in the whole program — it is how you tell a legal move from
an illegal one. The old engine answered it by maintaining a database of every attack on
every one of the 64 squares, and rebuilding that database after every trial move. This
engine answers it by walking outwards from the square in question, about forty array reads,
in `eng_IsAttacked`. Legality testing went from the most expensive thing the program does to
one of the cheapest, and 2.4 KB of RAM went away with the database.

The target is a 1 MHz 6502 with a few kilobytes to spare, so the cost of everything here was
measured on real hardware rather than reasoned about. Several sections below end with a
number that overturned the design that preceded it; those are kept because the number is the
interesting part.

---

# Part I — Holding a position

## 1.1 The 0x88 board

The board is `char geBoard[128]` — 128 squares for a 64-square game.

Picture it as a 16-wide grid, 8 ranks tall. The left half (files 0–7) is the real chessboard;
the right half (files 8–15) is a dead zone that exists only to be walked into. A square's
number is `rank * 16 + file`, so the layout in hex is:

```
        file:  0  1  2  3  4  5  6  7   |  8  9  a  b  c  d  e  f
rank 8 (row 0) 00 01 02 03 04 05 06 07  | 08 09 0a 0b 0c 0d 0e 0f
rank 7 (row 1) 10 11 12 13 14 15 16 17  | 18 ...
...
rank 1 (row 7) 70 71 72 73 74 75 76 77  | 78 ...
                  the board                  the dead zone
```

Note **row 0 is rank 8**. Black's back rank is at the low addresses, White's at the high
ones, which matches how a board is printed and how the old `gChessBoard` was laid out. The
consequence to remember: **White pawns move by −16, Black pawns by +16** (`WHITE_PUSH` /
`BLACK_PUSH` in `engine.c`).

The point of the wasted half is this one macro:

```c
#define ENG_OFFBOARD(sq)   ((sq) & 0x88)
```

`0x88` is `10001000` in binary — bit 3 is "file ≥ 8", bit 7 is "rank ≥ 8". A square is on
the real board if and only if both are clear. So the off-board test is a single AND against
a constant, which on a 6502 is two instructions.

Why this matters more than it looks: a move generator's inner loop is "take a square, add an
offset, is the result still on the board?" With a plain 64-square array you compute a rank
and a file and test both, and because `char` is unsigned on cc65, a knight stepping off the
left edge wraps around to the right edge of the rank below and *looks legal*. The old
generator was a thicket of `x > 7 || y > 7` checks guarding against exactly that. In 0x88
the wrap lands in the dead zone and the AND catches it. A whole class of bug is designed out
rather than defended against.

Sliding pieces get this for free too: `for(to = from + step; !ENG_OFFBOARD(to); to += step)`
walks a ray and stops at the edge automatically (`genSlider`).

**The UI never sees a 0x88 square.** `plat.h` and every platform file use the original 0–63
tile numbering, and `ENG_TO_TILE` / `ENG_FROM_TILE` convert at the boundary. This is a hard
rule, not a style preference: when the engine was replaced, several ports (atari, cx16 and
apple2 among them) could not be built or tested on the development machine at all, and
freezing that interface is what let the entire engine be replaced without editing a single
platform file. Most of them can be built and run now. The rule stays, because it is what
made that possible.

## 1.2 The piece byte

One byte per square (`types.h`):

- bits 0–2 (`PIECE_DATA`) — the type: `NONE, ROOK, KNIGHT, BISHOP, QUEEN, KING, PAWN` (0–6)
- bit 7 (`PIECE_WHITE`) — set for White

So `geBoard[sq] & PIECE_DATA` is the kind, `COLOR_OF(piece)` is the side (0 = Black,
1 = White, matching `SIDE_BLACK` / `SIDE_WHITE`), and `NONE` is a valid piece type meaning
"empty square". Note the type ordering is historical and is *not* by value — that costs a
small lookup table in the search, see §6.4.

There used to be a `PIECE_MOVED` bit, used to decide castling rights and pawn double-steps.
It is gone. Double-steps are decided by which rank the pawn is on, and rights live in the
position state below — which is the correct home for them, because rights have to be
*restored* when a move is taken back, and a bit spread across several pieces cannot be.

## 1.3 The state that is not on the board

Four facts about a position are invisible in the piece layout, and all four live as globals
in `engine.c`:

```c
char geCastle;    // ENG_CASTLE_WK|WQ|BK|BQ, one bit each
char geEP;        // the square a pawn may capture onto, or ENG_NO_SQUARE
char geHalfmove;  // plies since the last capture or pawn move (the 50-move rule)
char geKing[2];   // where each king stands, indexed by side
```

`geKing` is pure economy: the question "is my king in check?" is asked millions of times per
move, and without this you would have to *find* the king first, scanning up to 64 squares.
`eng_Make` keeps it current in the two lines that move a king.

## 1.4 The move

```c
typedef struct tag_engMove {
    char m_from, m_to;   // 0x88 squares
    char m_flags;        // what kind of move it is
    char m_score;        // scratch space for move ordering
} t_engMove;
```

The flags (`ENG_MF_*` in `engine.h`):

| Bits | Meaning |
|---|---|
| 0–2 | the piece a promoting pawn becomes — nonzero *means* "this is a promotion" |
| 3 | en passant capture |
| 4 / 5 | castling, king-side / queen-side |
| 6 | pawn double push (this is what sets `geEP`) |

Packing the promotion piece into the same bits that flag the promotion is neat: `flags &
ENG_MF_PROMO` is both the test and the value, and it is already a valid piece type, so
`promote | (piece & PIECE_WHITE)` is the promoted piece byte (`eng_Make`).

The struct is four bytes rather than three, deliberately. Four means indexing an array of
moves is a shift instead of a multiply — the 6502 has no multiply instruction — and it
provides the byte the search needs for move ordering. Three bytes would have been smaller and
slower.

---

# Part II — Playing a move and taking it back

A search tries hundreds of moves per second and takes almost all of them back. How you do
that is a load-bearing design decision.

The obvious approach is copy-and-restore: save the whole position, make the move, and on the
way back copy the saved position over the top. That is 128 bytes of copying per move at every
node. The old engine did something even worse — it made the move, *rebuilt the entire attack
database*, looked at the result, and rolled back.

This engine uses **make/unmake**: `eng_Make` modifies the board in place and writes down only
what cannot be worked out in reverse; `eng_Unmake` reads that back and undoes it.

```c
typedef struct tag_engUndo {
    char m_captured;   // piece taken, NONE if none
    char m_ep;         // en passant square before the move
    char m_castle;     // castling rights before the move
    char m_halfmove;   // fifty-move counter before the move
} t_engUndo;
```

Four bytes. Why exactly these four, and nothing else?

Because everything else is recoverable from the move itself. Where the piece came from is
`m_from`. What it was is on the board at `m_to`. Whether a rook moved as part of castling is
in the flags. But the four fields above are *destroyed* by making the move and cannot be
inferred afterwards:

- **`m_captured`** — the captured piece is overwritten. Nothing on the board remembers it.
- **`m_ep`** — every move clears `geEP`. Whether there *was* an en passant square before is
  gone.
- **`m_castle`** — rights are lost monotonically. Knowing that the White king is on g1 does
  not tell you whether Black still had its queen-side right before your move.
- **`m_halfmove`** — reset to zero by any capture or pawn move.

The three awkward moves each need a special case, and all three are in both functions,
mirrored:

- **En passant** — the captured pawn is *not* on `m_to`, it is beside the capturing pawn. So
  `eng_Make` clears a different square than the one it fills, and `eng_Unmake` restores it.
- **Castling** — the rook moves too, so both functions move a second piece.
- **Promotion** — the piece that arrives is not the piece that left. `eng_Unmake` computes
  `moved` — the mover *as it stood before the move* — by turning a promoted piece back into a
  pawn. That local is not decoration; it is what the evaluation delta is given, and it must
  match what `eng_Make` was given exactly. See §5.3.

Castling rights are revoked in three places, and the third is easy to forget: when a king
moves (both rights), when a rook moves off its home square, **and when a rook is captured on
its home square**. `revokeRights(square)` handles the last two by taking a square rather than
a piece, and `eng_Make` calls it both on `to` (a capture landing there) and on `from` (a rook
leaving).

**Why this pair is the most correctness-critical code in the engine.** A search makes and
unmakes millions of times. If `eng_Unmake` fails to restore the position *exactly*, the board
silently drifts, and the search is then reasoning about a position that no longer exists. The
symptom is not a crash — it is an engine that plays illegal or nonsensical moves much later,
with no clue where the damage began. This is why the project validates it with `perft`
(§4.5) rather than by inspection.

---

# Part III — Attacks, asked rather than stored

## 3.1 The ray-cast

`eng_IsAttacked(square, bySide)` answers "does anything belonging to `bySide` attack this
square?" by standing on the square and looking outwards:

1. Two pawn squares.
2. Eight knight squares.
3. Eight king squares.
4. Four orthogonal rays, until something blocks — if the blocker is a rook or queen of the
   right colour, yes.
5. Four diagonal rays, same, for bishop or queen.

That is roughly forty board reads worst case, usually fewer because rays hit blockers early.

The pawn test is the one that trips people reading the code, so it is worth spelling out.
A White pawn on square *p* attacks *p*−17 and *p*−15 (up and to each side, remembering that
up is negative). We are asking the reverse question: which squares could a White pawn be
standing on to attack *us*? It must be at *square*+17 or *square*+15. The code encodes that
by flipping the push direction:

```c
back = (bySide == SIDE_WHITE) ? -WHITE_PUSH : -BLACK_PUSH;   // +16 for white
sq = square + back - 1;   // = square + 15
sq = square + back + 1;   // = square + 17
```

**The insight worth taking from this section:** answering "is X attacked?" is *cheaper* than
maintaining the answer for all 64 squares, by a wide margin, because you almost never want
all 64. The old engine's attack database was a classic case of caching something whose
computation was cheaper than its invalidation.

## 3.2 What replaced the database

`eng_AttackersOf(square, bySide, list)` is the same walk, but it collects every attacker
instead of stopping at the first, and returns the count.

This is what the `A` / `D` / `B` visualizer displays are built on — the feature this program
exists for. The `A` and `D` displays need one query, for the square under the cursor
(`board_AttackersOf` in `board.c`, which converts to 0–63 tiles on the way out). The `B`
whole-board overlay needs 128 queries — 64 squares × 2 sides — which is why
`board_RefreshAttackCounts` is only called when that display is actually switched on.

The platform files still read `gpAttackBoard[giAttackBoardOffset[tile][side]]`, because that
name is part of the frozen interface. But it now holds only the *count* per tile per side —
128 bytes, where the database was 2176 bytes plus a 256-byte offset table. No port ever read
the attacker list, only the count, so nothing was lost.

`eng_InCheck(side)` is one line on top of all this: `eng_IsAttacked(geKing[side], 1 - side)`.

---

# Part IV — Move generation

## 4.1 Pseudo-legal, then filter

A move is **pseudo-legal** if the piece moves the way that piece moves, and **legal** if it
also does not leave your own king in check. The second condition is awkward because it is not
a property of the piece moving — a bishop on d2 might be pinned against the king by a rook on
h6, and nothing local to the bishop says so.

The engine takes the standard route: generate pseudo-legal moves, then test each one by
**making it, asking whether the king is now attacked, and unmaking it**.

```c
eng_Make(&moves[i], &undo);
if(!eng_IsAttacked(geKing[side], 1 - side))
    /* keep it */;
eng_Unmake(&moves[i], &undo);
```

That is `eng_GenLegalMoves`, and the same three lines appear inline in `negamax`, `quiesce`,
`searchRoot`, `search_Outcome` and `board_LegalMovesFrom`. It handles pins, discovered
checks, and moving out of check with no special cases at all, because it is not reasoning
about geometry — it is just looking.

There is a cleverer way (work out the pinned pieces once per position and skip the test for
everything else). It was planned, measured, and dropped; §6.9 explains why, because the
reason is more interesting than the technique.

## 4.2 The generator

`genBoard` walks squares 0x00 to 0x77, skips the dead zone, and calls `genFrom` for each
piece of the side to move. `genFrom` switches on the piece type into one of three shapes:

- **`genStepper`** — knight and king: eight fixed offsets, each tried once.
- **`genSlider`** — rook, bishop, queen: walk each ray until you leave the board or hit a
  piece; if that piece is an enemy, the capture is a move and the ray stops.
- **`genPawn`** — the awkward one: forward one, forward two from the home rank if both
  squares are clear, two diagonal captures, en passant, and promotion.

Two details in `genBoard` are there for measured reasons rather than tidiness. It repeats the
"is there a piece of mine here?" test that `genFrom` also does, because doing it in the
caller turns 64 function calls per generation into about 16, and on a 6502 a four-argument
call is not cheap. And `addMove` checks the caller's capacity (`sc_maxMoves`) on every write
— see §4.6.

## 4.3 A promotion is four moves

`addPawnMove` expands a pawn arriving on the back rank into four separate moves — queen,
rook, bishop, knight — each with the piece packed into the flag bits.

The old engine produced *one* move and asked afterwards what to promote to (always a queen
for the AI, a menu for a human). That is a legitimate UI shortcut and a fatal engine bug:
under-promotion to a knight is occasionally the only move that wins, and a search that cannot
generate a move cannot find it. It also showed up cleanly in testing — the old generator's
node counts came up short by exactly 3 for every promoting move.

The UI still asks the human which piece they want; `board_IsPromotion` detects the case and
`board_FindMove` matches the chosen piece back to the generated move. `board_LegalMovesFrom`
collapses the four back into one destination tile for display.

## 4.4 Castling is generated only when fully legal

Castling has four conditions beyond "the rook and king have not moved": the squares between
must be empty, the king must not be in check, it must not cross an attacked square, and it
must not land on one. `genCastle` checks the first three itself and lets the ordinary
make/test/unmake filter catch the fourth.

This breaks the pseudo-legal-then-filter pattern, on purpose: the "crossed square" condition
is not about the king's final position, so the generic filter *cannot* see it. Testing it
here costs two `eng_IsAttacked` calls; the alternative is a special case in the filter. The
comment in `engine.c` says as much.

## 4.5 How this is known to be correct

Move generation is where chess engines hide their bugs, because a rare mistake — an en
passant capture that should have been illegal, a castling right not revoked — produces a
legal-looking game that is subtly wrong.

The standard instrument is **perft**: count the leaf nodes at a fixed depth from a known
position and compare against published numbers. It is a brutally effective test, because a
single wrong move anywhere in the tree changes the total, and the reference figures are exact
integers computed by other engines.

This generator matches the reference exactly to depth 5 on all five standard test positions,
including 193,690,690 nodes on "Kiwipete" (a position constructed specifically to catch
castling and en passant errors), and to depth 6 on the two positions that publish one. The
positions and their reference counts are in `tests/engineperft.c`.

If you change anything in `engine.c`, run perft. A mismatch is a stop-the-line event.

## 4.6 The capacity argument, and a bug worth knowing about

`eng_GenMoves` takes `maxMoves` and `addMove` refuses to write past it. This looks like
belt-and-braces and is not.

The search hands out slices of one shared arena (§6.1). The first version of the generator
wrote its moves and *then* checked whether they fitted — which overran the arena into the
statics declared after it, namely the node counter, the node budget and the abort flag. The
symptom was the node budget being silently ignored. Nothing about that symptom suggests a
buffer overrun, and it cost real time to find.

The lesson generalises past this program: a buffer overrun into adjacent *statics* does not
crash, it corrupts control flow, and the resulting misbehaviour points anywhere but at the
real cause.

## 4.7 The capture-only generator

`eng_GenCaptures` produces captures, en passant and promotions only. It exists for the
quiescence search (§6.5), which searches captures and would otherwise pay for a full
generation to look at two or three moves. Measured over the perft trees, captures are 17% of
all moves — so the full generation was doing roughly six times the work needed.

It is not a second generator. It is the same code with one static flag, `sc_capturesOnly`,
that suppresses the *emission* of quiet moves while walking exactly the same squares in
exactly the same order. Sliders still walk the whole ray (they must, to find the blocker),
they just do not write down the empty squares on the way.

That buys a property the change depends on: **the output is an exact subsequence of what
`eng_GenMoves` would have produced, in the same order.** That is not tidiness. The search
breaks score ties by list position (`pickBest` keeps the first of equals), so a generator
producing the right *set* in a different *order* would leave the engine playing different
moves in tied positions — still legal, still plausible, and quietly not the engine that was
measured. `tests/quiescence.c` checks set and order against the filtered full generator at
185,939 positions.

Note the one exception in `genPawn`: a promotion by a plain push is not a capture, but
quiescence has always searched promotions, so the captures-only pass keeps them.

---

# Part V — Evaluation: what a position is worth

## 5.1 Position value, not move preference

This is the conceptual centre of the rewrite, so it is worth being slow about.

`eval_Position(side)` returns a number in **hundredths of a pawn** (so a pawn is 100, a knight
320) saying how good the position is *for `side`*. Positive is good. It is a pure function of
the board: the same position always scores the same, regardless of how it was reached or
whose turn it is to think about it.

The engine this replaced scored *moves*, not positions — it computed how much it wanted to
play a particular move, using heuristics about capturing and defending. Why is that fatal?

A search works by passing scores up a tree. At a leaf you have a number; the layer above
takes the best of its children; the layer above that takes the best *for the other side*,
which is the worst for you. Both operations assume the number means the same thing at every
level and can be negated to switch perspective. A position value satisfies that: if a
position is +150 for White it is −150 for Black, necessarily, because it is one fact about
one board. A move-desirability score satisfies neither. "I want to play Nxe5, +40" is not a
fact about a position; it is not comparable with a score computed two plies deeper, and
negating it produces nothing meaningful. The old engine's minimax was therefore accumulating
quantities that could not validly be accumulated.

That is the difference between an engine that plays and an engine that only appears to.

## 5.2 What it actually measures

Two terms, in `eval.c`:

**Material.** `gcPieceValue`: pawn 100, knight 320, bishop 330, rook 500, queen 900. The king
is 0 — losing it is not a score, it is the end of the search, and it is handled by mate
detection instead (§6.7).

**Piece-square tables.** A 64-entry signed table per piece type saying how much that piece
likes standing on each square. A knight on the rim is −50, in the centre +20. Pawns are
rewarded for advancing, more so in the centre. The king's table rewards staying home behind
its pawns.

Piece-square tables are the cheapest strength in chess programming: they encode "knights
belong in the centre, rooks belong on the seventh, keep your king castled" as a single array
lookup, with no reasoning at all. Measured here at 138 wins to 10 losses over 256 self-play
games against material-only. Not a close call.

The tables are written the way a board prints, index 0 = a8 to index 63 = h1, from White's
point of view. **Black reads the same table with the rank flipped**, which is `tile ^ 56` —
XORing the top three bits of the tile index inverts the rank and leaves the file alone. One
set of tables serves both colours.

`pieceScore` returns everything **white-positive** — White's pieces score positive, Black's
negative — so the totals add up without anyone tracking whose turn it is. `eval_Position`
negates at the very end if the caller asked as Black.

## 5.3 The running total, and the rule that keeps it honest

`eval_Position` used to walk all 64 squares. At a million nodes a move on a 1 MHz machine,
anything done per-piece per-node is paid for twenty thousand times per move — and that cost
is what blocked the evaluation from being *improved*, because every new term made every node
slower (§5.4).

So the score is now a running total, `geEvalScore`, maintained by make/unmake:

```c
// eng_Make
geEvalScore += eval_MoveDelta(move, piece, undo->m_captured);
// eng_Unmake
geEvalScore -= eval_MoveDelta(move, moved, undo->m_captured);
```

`eval_Position` is now a read (and a negate for Black). `eval_Refresh` does the full walk and
is called only where pieces reach the board without going through `eng_Make` — new game, FEN
parsing in tests, and once per move at the top of `search_Best` so the search can never
inherit a stale total.

**The rule that makes this safe is worth internalising**, because it is a general technique:

> `eval_MoveDelta` is a pure function of the move and the two piece bytes it is handed. It
> reads no board state at all.

Make adds it; unmake subtracts *the same call with the same arguments*. Because the function
touches nothing else, the two cannot disagree — the pair is correct **by construction rather
than by care**. If the delta read `geBoard`, unmake would be reading a board in a different
state than make did, and the total would drift by an amount that depends on the position. A
drifting evaluation does not crash; it just makes the engine gradually wrong in a way no
assertion catches.

This constrains what can be added to the evaluation. A term folds in cheaply only if it is a
property of *a piece on a square*. Material and piece-square tables qualify. Pawn structure
(doubled, isolated, passed pawns) does not, because it is a property of the whole pawn
configuration — it would need a per-file pawn count carried alongside the score.

`tests/gamefuzz.c` checks the running total against a full recount after every move, undo and
redo, over 300 games weighted toward castling, en passant and promotion — the three moves
whose delta is not simply "a piece left one square and arrived on another".

## 5.4a The endgame tables, and what a second running total costs

A pawn is worth more the closer it is to promoting, and a king belongs in the
middle of the board once the queens are off. Both are standard, and both were out of reach
while the evaluation was one set of tables: the right square for a pawn depends on the phase,
and deciding the phase per piece per node is exactly the cost that killed §5.4's two terms.

**The trick is to carry the difference rather than a second score.** `geEvalScore` stays the
middlegame total it always was — nothing that reads it changes — and a second running total,
`geEvalEnd`, holds only what the endgame tables would *add*. Both are maintained by
make/unmake by the same rule as §5.3, so both are paid once per move made. `eval_Position`
blends them by how much non-pawn material is left, in four steps, using shifts rather than a
multiply, and leaves immediately in the middlegame:

```c
score = geEvalScore;
if(gePhase < PHASE_ENDGAME)
    score += weighted(geEvalEnd);   // 1/4, 1/2, 3/4, all
```

`geEvalEnd` is zero for every piece but pawns and kings, since no other table changes with the
phase. Three running totals now ride along with the board — score, hash, phase — and the
fuzzer checks all of them against a full recount after every move, undo and redo.

**What it was worth**, 512 games a set, and the order matters:

| | equal nodes | equal time (C64's 9%) |
|---|---|---|
| endgame king table alone | +15 Elo, 1.55σ | not measured — it did not earn it |
| king **and** pawn tables | **+44 Elo, 4.41σ** | **+30 Elo, 3.02σ** |

And on the number the work was actually for — the share of clear material advantages that
become wins — 69% to **78%** from openings, 87% to **90%** from endgame positions. Games that
ran out the 240-ply limit halved.

**The king alone was nearly worthless, and that is the lesson.** It measured +15 Elo and moved
conversion not at all. The engine could not finish won endings because marching a pawn from
home to the seventh earned it 45 centipawns — nine a move — while the promotion that justifies
the march is worth 800 and sits past the horizon. Giving the king somewhere to go without
giving the pawns a reason to move fixed nothing. The two terms are worth three times together
what one was worth alone.

## 5.4 Two good terms that were removed

The comment block in `eval.h` records two evaluation terms that were built, measured, and
taken back out. Both stories are useful.

**King safety by counting the pawn shield: a genuine loss.** Over 512 self-play games it came
out at −2.6 sigma, and removing it was itself a measured win. The first theory was that it
lacked an endgame check, so a material gate was added — no difference, which killed that
theory too. The narrow conclusion is not "king safety is bad" but "*counting pawns in front
of the king* is bad"; a term based on how many enemy pieces bear on the squares around the
king may still be worth trying, and the comment says so, so nobody re-adds the pawn-shield
version by accident.

**Pawn structure: a win at equal nodes, nothing at equal time.** It scored +2.0 sigma over
512 games when both sides searched the same number of nodes. But it made every node 1.35x
more expensive on a real C64, and at equal *time* — which is what a player actually
experiences — it came to +0.6 sigma, indistinguishable from noise, for 735 bytes.

**The general lesson is the second one.** A richer evaluation buys strength by making every
node slower, and on a 1 MHz machine that trade is not a rounding error. The test that decides
is equal time, not equal nodes, and it overturned every answer the equal-node test gave.

Both were blocked on the same thing — the from-scratch evaluation — and making it incremental
is what unblocks them. A term that folds into `eval_MoveDelta` is paid once per move made
rather than once per node. They remain unbuilt; the note in `eval.h` says what each would now
cost.

There is also a sub-lesson here worth a line. When the endgame king table looked too
expensive, the first theory was that summing material into an `int` was the cost, and it was
rewritten as a `char` count. That changed the timing by one jiffy in three thousand. **The
cost was not the arithmetic width, it was doing anything at all per piece, thirty-two times,
at every node.**

## 5.5 Mate scores

```c
#define EVAL_INFINITY     30000
#define EVAL_MATE         29000
#define EVAL_MATE_IN(ply) (EVAL_MATE - (ply))
```

A whole side's material is about 4000, so mate scores sit far above anything the evaluation
can produce and are never confused with a material advantage. `EVAL_INFINITY` is the initial
alpha/beta window — a value no real score can reach.

Subtracting the ply is what makes the engine *finish* games. A mate in two scores 28998 and a
mate in four scores 28996, so the search prefers the quicker kill; and when losing, it prefers
the longest resistance. Without that, all mates score identically and the engine will happily
play a move that mates in seven when mate in one is available — or, worse, shuffle forever in
a position it knows is winning.

---

# Part VI — The search

This is where the engine actually plays chess. `search.c`.

## 6.1 The arena

All move lists come from one static array:

```c
static t_engMove st_arena[SEARCH_ARENA];   // 512 moves, 2 KB
static unsigned int si_arenaTop;
```

Each node takes a slice from the top, generates into it, and restores `si_arenaTop` on the
way out — a stack allocator. There is a hard reason it is not just a local array in
`negamax`: **cc65 allows a function only 256 bytes of locals**, and a full move list is 512
bytes. A board-sized array on the stack compiles fine on the host and does not survive
contact with the target.

Nodes check `arenaRoom()` before generating and bail out to a static evaluation if there is
not enough. This produced one of the more instructive findings in the project — see §6.5.

## 6.2 Minimax, and negamax

The idea underneath everything: I pick the move that maximises my score; my opponent then
picks the move that minimises it; and so on, alternating, down to some depth. At the bottom
you call `eval_Position`. Scores propagate back up, alternating max and min at each level.
That is **minimax**, and with a perfect evaluation and infinite depth it plays perfect chess.

Writing max and min as separate cases doubles the code. **Negamax** removes the duplication
using the symmetry established in §5.1: because a position worth +X to me is worth −X to my
opponent, "minimise" is the same operation as "maximise, then negate". So there is one
function, and the recursive call reads:

```c
score = -negamax(1 - side, depth - 1, -beta, -alpha, ply + 1);
```

Everything is flipped in one place: the side, the sign, and the window.

## 6.3 Alpha-beta

Plain minimax examines every node, which at a branching factor of ~35 is hopeless past three
or four ply. **Alpha-beta** cuts the tree without changing the answer.

`alpha` is the best score I am already guaranteed elsewhere. `beta` is the best the opponent
is already guaranteed. The cutoff is one line:

```c
if(score >= beta)
    return beta;
```

The plain-language version: *I have found a reply so good that my opponent would never have
let me get here.* Suppose the opponent already has a line worth 50 to them. I am now looking
at a move of theirs where I have found a reply worth 60 to me. Whatever else I might find in
this subtree can only be as good or better for me — which makes this branch worse and worse
*for them* — so they will simply play their other line instead. Nothing further in this
subtree can affect the final answer, so stop looking.

Alpha-beta returns the *identical* move to full minimax. It only skips work whose result
cannot matter. That is what makes it safe to use unconditionally.

The catch, and it is large: **how much it prunes depends entirely on move ordering.** The
cutoff above only fires if a good move is found early. Search the best move first at every
node and you examine roughly the square root of the tree — depth 6 for the cost of depth 3.
Search them worst-first and alpha-beta is barely better than plain minimax. This is why the
next section is not polish.

## 6.4 Move ordering

Two mechanisms, both in `scoreMoves`, which writes a rough priority into each move's
`m_score`. The numbers are never compared to anything but each other:

**MVV-LVA — Most Valuable Victim, Least Valuable Attacker.** Try captures first, best victim
first, and among equal victims prefer capturing with your cheapest piece. Taking a queen with
a pawn is the best thing that can happen; taking a pawn with a queen is usually a mistake.

```c
moves[i].m_score = 150 + (sc_mvvRank[victim] << 3) - sc_mvvRank[attacker];
```

The victim rank is shifted left 3 so it dominates completely, with the attacker rank as a
tiebreak within it. `sc_mvvRank` exists because the piece constants in `types.h` are not in
value order (`ROOK` is 1, `PAWN` is 6), so it maps type to worth: `{0, 4, 2, 3, 5, 6, 1}`.

Promotions score above captures — a queen promotion at 200, others at 120 — because a queen
appearing is usually the best move on the board.

**Killer moves.** Two slots per ply (`st_killers[ply][2]`), holding quiet moves that caused a
beta cutoff at this depth in a *sibling* position, scored at 100. The intuition is that chess
positions at the same ply are usually similar — if pushing that pawn to fork two pieces
refuted one of the opponent's tries, it very likely refutes the next one too. Only quiet
moves are recorded, because captures are already ordered by MVV-LVA and would waste the slot.
`recordKiller` shifts the old killer down rather than overwriting it, so there is a primary
and a backup.

`pickBest` does a **selection pass, not a sort**: on each iteration it swaps the highest-scoring
remaining move into position `i`. This is the right shape, because a cutoff usually lands
after the first two or three moves and the rest are never examined — a full sort would pay to
order moves nobody looks at. Measured on this engine: the search tries **2.28 moves per
generating node** before a cutoff. That number matters again in §6.9.

## 6.5 Quiescence: the horizon effect

Here is the problem a fixed depth creates. Suppose the search runs out of depth exactly after
you capture a defended pawn with your queen. It calls `eval_Position`, which reports that you
are a pawn up, and it believes it. The recapture that wins your queen is one ply past the
horizon and is never seen.

This is the **horizon effect**, and it is not a rare edge case: it happens at every leaf of
every search, and it makes an engine hang pieces constantly. Adding a ply does not fix it, it
only moves the horizon.

The fix is **quiescence search** (`quiesce`): at depth 0, do not evaluate — keep searching,
but only through captures, until the position is *quiet* and the static evaluation is
trustworthy. Since captures are limited (material is finite), this terminates naturally.

The subtlety is **stand pat**:

```c
stand = eval_Position(side);
if(stand >= beta) return beta;
if(stand > alpha) alpha = stand;
```

A side is never *obliged* to capture. So before considering any capture, take the score for
doing nothing and use it as a floor. Without this, quiescence would force you into a losing
exchange just because a capture existed. With it, quiescence answers the right question:
"can I improve on standing still, by capturing?"

`SEARCH_MAX_PLY` (12) caps how far quiescence may run past the main search, so a pathological
capture chain cannot recurse forever.

**The finding that came out of this, which is worth more than the speed.** Quiescence used to
call `eng_GenMoves` and discard the quiet moves. Switching it to `eng_GenCaptures` (§4.7) was
supposed to be a pure speed change. It was also, unexpectedly, a strength change — and
instrumenting the arena high-water mark explained why:

| | arena high water | times exhausted |
|---|---|---|
| Full generation in quiescence | **512 of 512** | **1018** |
| Capture-only | 231 of 512 | 0 |

A long capture chain took a full-width slice at every ply and filled the arena, at which point
`quiesce` gave up and returned a static evaluation. Over one match workload that happened
1018 times — so quiescence was bailing out **precisely in the sharp positions it exists for**,
and the engine was quietly weaker than its node counts suggested.

Running out was never a *correctness* problem: the generator is handed its capacity and stops
(§4.6). It was a silent *strength* problem, and those are far harder to notice than a crash.

## 6.6 Iterative deepening

`search_Best` does not search to depth 5. It searches to depth 1, then depth 2, then depth 3,
and so on, keeping the best move from the last iteration that *finished*.

The obvious objection is that this repeats work. It does — and it costs almost nothing,
because the tree grows by roughly 5x per ply, so every iteration before the last one together
is a small fraction of the total.

What it buys is worth far more than that fraction:

1. **A move is always available.** Stop whenever you like and there is a complete, sensible
   answer from the last finished depth. This is what makes a node budget work at all.
2. **It makes the deep search faster, not slower.** The best move from depth *n*−1 is an
   excellent guess for depth *n*, and searching it first is exactly what alpha-beta needs
   (§6.3). `searchRoot` scores the previous iteration's best move at 255 so `pickBest` tries
   it first. The saving from better ordering typically exceeds the cost of the repeated
   shallow searches.

`search_Best` also refuses to *start* an iteration it cannot finish:

```c
if(depth > 1 && si_nodes > (si_budget / 3))
    break;
```

An abandoned iteration is pure waste — its work is discarded and the shallower move is played
anyway — so spending the last of the budget on one is the worst possible use of it. Measured
growth is about 5x per depth; the test uses a cautious 3x so a depth that might just make it
still gets its chance. Depth 1 always runs, because a move has to come back.

**Read that line again, because it is written as a division for a reason.** It was originally
`(si_nodes + si_nodes + si_nodes) > si_budget`. cc65's `unsigned int` is **16 bits**, so
3 × nodes wraps above 21845 — and level 4's budget is 60000:

| si_nodes | 3 × nodes as 16 bits | old test said | correct answer |
|---|---|---|---|
| 20000 | 60000 | go | go |
| **28868** | **21068** | **go** | **STOP** |
| **45000** | **3928** | **go** | **STOP** |

At level 4 the guard never fired at all. The host build has 32-bit `int`, so **no native test
can reproduce this** — the whole unit suite, the match harness and self-play all pass either
way. It was found by re-reading the arithmetic, not by a failing test. `si_nodes > si_budget / 3`
is arithmetically identical and cannot overflow.

The general rule: **the native test suite validates logic, never machine width.** Anything
that multiplies or accumulates toward a limit has to be read against a 16-bit `int` by hand.

## 6.7 Checkmate and stalemate, for free

The old engine had dedicated machinery for detecting mate — `board_CheckForMate`,
`board_UpdateAttackGrid`, `board_CheckLineAttack`, a fixup table — several hundred lines of
it. All of it is gone, replaced by the observation that mate is not a special condition. It
is just:

> no legal moves, and the king is attacked. If the king is not attacked, it is stalemate.

In `negamax` that is three lines at the bottom of the move loop:

```c
if(!legal)
    return inCheck ? -EVAL_MATE_IN(ply) : 0;
```

Stalemate returns 0 — a draw, worth exactly nothing to either side, which is correct and also
important: an engine that scores stalemate as a loss will avoid it when losing, and one that
scores it as a win will stumble into it when winning.

`search_Outcome(side)` is the same test as a standalone query for the UI: generate, look for
any legal move, and if there is none, report `OUTCOME_CHECKMATE` or `OUTCOME_STALEMATE`. Nine
lines. `board_ApplyMove` calls it after every move and layers `OUTCOME_CHECK` and the
fifty-move `OUTCOME_DRAW` on top.

`negamax` also returns 0 immediately when `geHalfmove >= 100`, so the search cannot convince
itself that shuffling forever is winning.

## 6.8 Node budgets and skill levels

The search is bounded by a **node count**, never by a clock:

```c
if(si_nodes >= si_budget) { sc_abort = 1; return 0; }
```

Three reasons. None of the eight target platforms share a timer. A node budget makes every
one of them play the *identical* game, which makes any bug reproducible everywhere. And an
accelerated emulator simply finishes sooner, with no code aware that anything changed.

The four skill levels are `(depth cap, node budget)` pairs in `gcSearchSkill`:

| Level | Depth cap | Node budget | Measured on a stock C64 |
|---|---|---|---|
| 1 Very Easy | 3 | 400 | 8.2 s mean, 12.5 s worst |
| 2 Easy | 4 | 1200 | 29.4 s |
| 3 Harder | 5 | 15000 | ~3.5 min |
| 4 Very Hard | 6 | 60000 | emulator territory |

**Which of the two numbers is the weakener is not obvious, and both answers were tried.**

The first table was tuned as though the *budget* set the time. It does not: `search_Best`
stops when it finishes its deepest iteration, so in a normal position a level spends what its
*depth* costs and hands the move back, and the budget only binds in expensive positions. The
old level 1 was depth 2 with 500 nodes — but depth 2 wants about 1150 nodes in a middlegame,
so from move four onward it burned the entire budget, threw the iteration away, and played
its depth-1 move. Fourteen seconds on a C64 to play one ply: the worst point on the curve,
where the whole budget is consumed and none of it changes the move.

The second attempt made the *depth cap* the weakener, capping level 1 at depth 1. That hit
the time target beautifully and **could not mate**. In a real game, up a queen, a rook and a
bishop against a bare king, it shuffled a rook between d8 and e8 for ninety-odd plies heading
for a fifty-move draw — every quiet move scored the same, and nothing could see a mate two
moves away.

**A cap is the wrong lever because what a depth costs depends on the position, and a cap does
not.** Depth 2 wants ~1159 nodes in a middlegame but only ~220 in an endgame. A cap set to
keep the middlegame quick therefore also blinds the engine in endgames where that same depth
was nearly free — which is exactly where the depth is needed to convert a won game.

A **budget** scales correctly by construction. Paired with "do not start an iteration you
cannot finish", it buys whatever depth the position can afford. Endgames get searched deeper
for nothing. So the final table is budget-driven, with the depth cap as a safety rail.

Level 1's 400 nodes is a floor, not a preference: at 300 it goes straight back to shuffling a
won endgame into a draw. It costs about 8 seconds a move against a 3–5 second target, and
that is a deliberate trade — **a "very easy" level that cannot beat a bare king is broken,
not easy.**

### The AI that gave up

One more thing lives in `searchRoot`, and it is there because of a real failure on a real
board: a human played, the turn came straight back, no *Think* message, no AI move. The AI
appeared to resign a sound position.

`search_Best` had returned `m_haveMove = 0`, which every caller reads as "no legal moves" —
i.e. mate or stalemate. It did that because the budget ran out before depth 1 finished. In
that position depth 1 alone wants 1404 nodes, against level 1's budget.

Three things are worth keeping from it.

**The recorded "worst case" was nothing of the kind.** The budget table said depth 1 costs
152 nodes on average and 335 at worst. That 335 was the worst of eighteen positions from a
single quiet self-played game — a sample, presented as a bound. Measured over genuinely sharp
positions, depth 1 alone costs 1404 in the bug position and 3228 on Kiwipete, an order of
magnitude above the "worst" on record. **A maximum taken from one self-played game is not a
maximum**, because the engine plays quiet moves and then samples the quiet positions it
created.

**The first fix did not fix it.** Keeping the partial result only helps if at least one root
move finished before the abort. Here the budget died inside the *first* move's subtree, so
nothing had been recorded — and `searchRoot` ended with `if(!legal) result->m_haveMove = 0;`,
which actively wiped a banked move, because `legal` counts searches that *completed* rather
than legal moves that *exist*. The working fix banks a legal move the moment legality is
established, **before** searching it, and drops that reset. Move ordering has already put the
most promising move first, so the banked move is a reasonable one, not an arbitrary one.

**The shape of the failure was the giveaway.** Running out of time is normal and must degrade
to a **worse move**, never to **no move**. Any code path where exhausting a budget produces
"nothing" instead of "something rougher" is wrong by construction, whatever the numbers say.
`tests/search.c` now checks that guarantee over four sharp positions at budgets down to a
single node — far below anything the skill table would use, because the property has to hold
at *any* budget.

## 6.9 Where the time actually goes

There is no profiler for a 6502 here, and there does not need to be one. Run the search
normally, then again with one component doing an extra redundant copy of its work; the
difference is that component's cost *in situ*, including call overhead a source-level model
would miss. Every doubled operation is either side-effect free or exactly self-reversing, and
identical node counts across runs prove the tree was untouched. The driver is
`tests/c64profile.c`.

Real C64, budget 1600, depth 3:

| Component | Share |
|---|---|
| Move generation | **41.9%** |
| — of which the 120-square board scan | 7.0% |
| make / unmake (including the eval delta) | 18.7% |
| Move ordering (`scoreMoves` + `pickBest`) | 18.2% |
| Legality (`eng_IsAttacked` after make) | 13.7% |
| Everything else | ~7.5% |

**This overturned the assumption the remaining optimisation work rested on**, and the reason
is the most transferable lesson in this document.

The plan had said: the hot path is `eng_IsAttacked`, not move generation — generation is only
about 8% of the time. The proposed fix was a **pin set**: work out once per position which
pieces are pinned against the king, and skip the legality test for every move that provably
cannot expose it. That reasoning is correct, and it is a large win.

It was measured on **perft**. Perft makes, tests and unmakes *every* generated move, so
legality naturally dominates. An alpha-beta search with working move ordering tries only
**2.28 moves per generating node** before a cutoff — most generated moves are never tested at
all. The ratio therefore inverts: generation 42%, legality 14%.

So the ceiling on a *perfect, free* pin set is 1/(1−0.137) = **1.16x**. The real thing removes
about 89% of the calls (king moves, en passant and genuinely pinned pieces still need
testing) and *adds* a pin computation of eight rays at every generating node, amortised over
only 2.28 calls. Net: about **1.07x** — for the highest-correctness-risk change left in the
plan, since the en passant horizontal pin (where two pawns leave a rank at once) is the
classic way these ship a legality bug.

The item was dropped on evidence, not on difficulty. **A cost measured on perft does not
transfer to a search that abandons most of its moves unexamined.**

The profile that remains is flat — 42 / 19 / 18 / 14 with no dominant hotspot, which is what a
reasonably optimised program looks like. There is no remaining change worth a ply.

## 6.10 Repetition, and the position history

For most of this engine's life the search could not tell a position from the same position two
moves later. Nothing scored a repeat as worthless, so in any position where it could not find
progress it would shuffle a piece back and forth indefinitely, and the fifty-move rule was the
only thing that ever ended it.

That was on the list of known absences for a long time, and it was assumed to be a small one.
It was not: measured over 512 self-play games, **62% of all games were drawn, every one of them
by repetition, and 57% of those were positions the engine itself scored as winning** (§5.1 of
`doc/strength.md`). Eighteen percent of the whole match was drawn while a rook or more ahead.

### The key

Detection needs to recognise a position it has seen, which needs a hash. The hash is built the
same way the evaluation is, and for the same reason — a per-node walk of the board is exactly
what the running total exists to avoid:

```c
// eng_Make
geHashKey ^= hashDelta(move, piece, undo->m_captured);
// eng_Unmake
geHashKey ^= hashDelta(move, moved, undo->m_captured);
```

`hashDelta` is deliberately the same shape as `eval_MoveDelta`, case for case: the mover, the
promotion, the en passant victim that is not on the target square, and the rook that castling
moves as well. It obeys the same rule that makes the evaluation safe — a pure function of the
move and the piece bytes, reading no board state — so make and unmake cannot disagree. XOR
being its own inverse means unmake applies the *identical* call rather than a second function
that has to undo the first correctly.

The table is 12 × 64 sixteen-bit values in `RODATA`, indexed by piece, colour and the 0..63
tile. Sixty-four wide rather than 128: the off-board halves of the 0x88 board would otherwise
double the table for nothing.

### What deliberately is not in the running key

Castling rights and the en passant file are folded in when a position is stored or compared,
not carried incrementally. That is two extra lookups at those points and no bookkeeping at all
in make/unmake.

**Rights have to be in there somewhere, and the reason is the case this feature exists for.** A
king that steps off e1 and comes back leaves every piece on the square it started on, and the
position is not the same one — it can no longer castle. That is a shuffle, which is precisely
the shape of position being judged, so a hash of piece placement alone would misfire exactly
where it matters most. `tests/repetition.c` plays those four king moves twice, from two
positions differing only in whether there is a castling right to lose, and requires opposite
answers.

### The history

The positions themselves live in a 128-entry ring, pushed by `eng_Make` and popped by
`eng_Unmake`. That placement is the whole design: because every move goes through those two
functions, the ring holds the real game's history and the search line currently being explored
in one array, with no second mechanism, nothing for a caller to remember, and no way for the
two to disagree. Undo and redo maintain it for free. So does the UCI adapter, which replays a
whole game move by move.

Two bounds keep the scan honest. It looks back no further than the fifty-move counter, since a
capture or a pawn move makes everything before it unreachable; and no further than the number
of entries actually pushed, because a position loaded from a FEN can arrive claiming a
fifty-move counter of forty with no history behind it at all. Without the second bound the scan
would walk forty entries into whatever the previous game left in the ring. Only every second
entry is compared — the others have the other side to move.

### One repeat, or three

The search treats a *single* repeat as a draw. A real game needs a threefold, and
`board_ApplyMove` applies that rule to the board, but inside a search line the third occurrence
tells you nothing the first did not: if a line returns to a position both sides could have
reached earlier, neither is making progress. Waiting for the third only means searching the
same shuffle twice more to reach the same answer.

Quiescence needs none of this. It searches captures and promotions only, and both reset the
fifty-move counter, so no position it reaches can repeat one above it.

### What it cost and what it bought

| | |
|---|---|
| RODATA | +1584 bytes (the table, plus rights and en passant) |
| BSS | +260 bytes (the ring) |
| CODE | +1038 bytes |
| Speed | **−9% on a real C64** |
| Strength | **+44 Elo** at equal nodes, **+38 Elo** at equal time, 512 games, 3.7 sigma |
| Self-play draws | 53% → 32%; decisive games 240 → 350 of 512 |

**The speed figure is the part worth stopping on.** Measured on this host — same 512-game
match, hash maintained but detection switched off, against a build with no hash at all,
identical node counts to the digit, best of eleven interleaved runs — the cost is 5.5%. On a
real C64, by `tests/c64search.c` under VICE, it is 9.2%, 8.7% and 9.4% at depths 2, 3 and 4.
The host understated the cost of the change by nearly half, because a 16-bit XOR and a table
index are one instruction there and several on a 6502, while everything they are being compared
against is comparatively cheaper. §6.9 records that a cost measured on perft does not transfer
to a search; this is its sibling, and the equal-time match above is run against the target's
number rather than the host's for exactly that reason.

### The honest weakness

The key is sixteen bits, so two different positions can collide, and a collision reads as a
draw. Bounding the scan by the fifty-move counter keeps the number of comparisons per node
small, which keeps the rate low — but low is not zero, and a false draw score costs a won
position rather than producing an error anybody would notice. The fix if it ever proves to
matter is a wider key, not a cleverer scan. Nothing in the measurements above suggests it
currently does.

This is also most of the groundwork for a transposition table, which is the other thing the
absence of incremental hashing was blocking.

---

# Part VII — The seams

The engine is deliberately unaware of the rest of the program. Everything above `engine.h`
speaks 0–63 tiles.

## 7.1 `board.c` — the adapter

What used to be the move generator and the attack database is now the adapter between engine
and display:

- **`board_SyncDisplay`** copies `geBoard` into `gChessBoard[8][8]` — a 64-byte display
  mirror in the old tile order that every platform file reads directly. Called after anything
  that moves a piece. It also refreshes the attacker counts, but only when the `B` display is
  on.
- **`board_LegalMovesFrom(tile)`** fills `gMoveTiles` with the legal destinations for the
  piece under the cursor. *Legal*, not pseudo-legal — a move that leaves the king in check is
  never offered, so there is no "Invalid" message to show any more. The four promotions
  collapse to one destination.
- **`board_FindMove(fromTile, toTile, promote, move)`** turns a cursor from/to pair back into
  the engine's move, which is where the human's promotion choice is matched up.
- **`board_ApplyMove`** makes the move, works out the outcome via `search_Outcome`, pushes the
  undo entry, and syncs the display. Every move in the game goes through here.
- **`board_AttackersOf`** is the visualizer's query, converting to tile numbers on the way
  out.

## 7.2 The frozen interface

The ground rule was that `plat.h` and the 0–63 tile numbering are frozen, because several
ports cannot be built or tested on the development machine. In practice the contract turned
out to be wider than `plat.h` — every port also reads three globals directly:

- `gChessBoard[y][x]` — pieces, for drawing
- `gpAttackBoard[giAttackBoardOffset[tile][side]]` — the attacker **count** only
- `gTile[0]`, `gTile[1]`, `gPiece[1]`, `gColor[0]` — the move log line, filled in by
  `undo_FindUndoLine`

All of it was honoured, and **no platform file was edited at all** during the rewrite. That
is the payoff for the conversion macros: the entire board representation, move generator,
search and evaluation were replaced underneath five ports without touching one of them.

## 7.3 Undo

The undo stack (`undo.c`) is a 128-entry circular buffer, so the last 127 moves can be taken
back. Each entry is 8 bytes: the move (from, to, flags), plus the four `t_engUndo` fields,
plus a state byte holding the outcome in bits 0–2 and the mover's colour in bit 7.

The key design point: **it records exactly what the engine needs, rather than trying to
reconstruct it.** `undo_Undo` rebuilds a `t_engMove` / `t_engUndo` pair with `loadEntry` and
calls `eng_Unmake` — the same function the search uses millions of times per move, so it is
by far the best-tested code in the program. `undo_Redo` calls `eng_Make` with the same pair.

The old undo stored five bytes and inferred castling and en passant from the move afterwards.
That reconstruction is where most of the undo bugs lived. This is the general point: storing
the state you will need is cheaper than deriving it, and derivation-on-the-way-back is a
reliable bug factory.

`sc_undoPtr`, `sc_undoTop` and `sc_undoBottom` are the current position, the redo limit, and
the oldest surviving entry. Making a new move sets `sc_undoTop = sc_undoPtr`, which discards
the redo branch.

`undo_FindUndoLine(linesBack)` walks back for the move log — note the warning in `undo.h`
that it sets `gTile`, `gPiece`, `gColor` and `gOutcome` as a side effect, which the platform
log windows rely on.

## 7.4 The turn

`main.c`'s `mainLoop` alternates sides, dispatching to `human_Play` or `cpu_Play` based on
`gUserMode`. `cpu_Play` is now the whole of what `cpu.c` does:

```c
search_Best(side, gcSearchSkill[gSkillLevel].m_depth,
            gcSearchSkill[gSkillLevel].m_nodes, &result);
if(!result.m_haveMove)
    return search_Outcome(side);
outcome = board_ApplyMove(&result.m_move, side);
```

Ask, play, redraw. The heuristics, sorted per-piece move arrays and single-line "sub tree"
walk that used to fill this file are gone.

`human_Play` (`human.c`) runs the cursor, the `A`/`D`/`B` toggles, and the promotion menu,
and ends at the same `board_ApplyMove`.

---

# Part VIII — The constraints that shaped all of this

The target is a 1 MHz 6502 with a few kilobytes free. Some of the design only makes sense in
that light.

**cc65's `int` is 16 bits and `char` is unsigned.** Almost everything is `char`. Any
expression that multiplies or accumulates toward a limit has to be checked by hand against
16 bits — §6.6 is what happens when it is not, and the host build's 32-bit `int` means no
native test will ever catch it.

**A function gets 256 bytes of locals.** This is why move lists come from the shared arena
and never from the stack (§6.1).

**No multiply instruction.** Hence the 4-byte move struct (index by shift), the `<< 3` in
MVV-LVA, and `tile ^ 56` for the piece-square flip.

**RAM is the binding constraint, not ROM.** Deleting the 2176-byte attack database and its
256-byte offset table paid for the entire search — the 2 KB arena, the killer table and the
undo stack — with room left over. The whole rework came out *smaller* than what it replaced.

**Determinism across ports beats adaptive timing.** Hence node budgets rather than clocks
(§6.8).

**Measure on the target.** Two regressions were introduced by changes made for testability
and safety, both invisible until measured on hardware: a tuning switch that made cc65 keep a
temporary `int` per piece, and a generator refactor that turned one inline switch into 64
function calls. Both were fixed by structure — the shipping build takes a different branch,
and the caller pre-filters empty squares. **Anything added to the evaluation or the generator
has to be measured on the target, not assumed free.**

---

# Part IX — Finding your way around

## Suggested reading order

1. **`engine.h`** — the whole board interface on two screens. Start here.
2. **`engine.c`** — read in this order: `eng_IsAttacked`, then `eng_Make` / `eng_Unmake`,
   then the generators. The first is the idea, the second is the machinery, the third is the
   detail.
3. **`eval.h`** — the header comment is the argument for the whole rewrite. Then `eval.c`,
   which is mostly tables.
4. **`search.h`**, then **`search.c`** from the bottom up: `search_Best`, `searchRoot`,
   `negamax`, `quiesce`. The top-level control flow makes the recursion easier to read than
   the other way round.
5. **`board.h`** and **`undo.h`** for the seams.

## Where things are

| I want to... | Look at |
|---|---|
| understand the board layout | `engine.h` header comment, `ENG_OFFBOARD` |
| see how legality is decided | `eng_GenLegalMoves`, and the same pattern inline in `negamax` |
| change how a piece moves | `genPawn` / `genStepper` / `genSlider` in `engine.c` |
| change how positions are judged | the `sc_pst*` tables and `pieceScore` in `eval.c` |
| add an evaluation term | `eval_MoveDelta` — and read the rule in `eval.h` first |
| change the AI's strength | `gcSearchSkill` in `search.c` |
| understand a cutoff | `negamax`, the `score >= beta` branch |
| see why a move was picked | `searchRoot` — and note it banks a move before searching it |
| trace a move from keypress to board | `human_Play` → `board_FindMove` → `board_ApplyMove` → `eng_Make` |
| find the visualizer | `board_AttackersOf` → `eng_AttackersOf` |

## The tests, and what each is for

| Test | Answers |
|---|---|
| `tests/engineperft.c` | is the move generator exactly correct? (the non-negotiable one) |
| `tests/gamefuzz.c` | does the incremental evaluation ever drift from a full recount? |
| `tests/quiescence.c` | is `eng_GenCaptures` an exact subsequence of `eng_GenMoves`? |
| `tests/castle.c` | the castling scenario matrix |
| `tests/search.c` | does a search always return a move, at any budget? |
| `tests/selfplay.c` | engine vs engine, one game |
| `tests/match.c` | configuration A vs B over hundreds of generated openings |
| `tests/budget.c` | how many nodes does each depth actually cost? |
| `tests/c64*.c` | the on-target measurements, run under VICE |

Build and run:

```bash
cd tests && make test
```

```bash
cc -Isrc -lcurses -funsigned-char src/globals.c src/engine.c src/eval.c src/search.c src/board.c src/undo.c src/cpu.c src/human.c src/frontend.c src/main.c src/term/platTerm.c -o /tmp/chessterm
```

```bash
make OPTIONS=optspeed TARGETS=c64
```

## On measuring strength

Two rules the project learned the hard way, both worth respecting before you "improve"
anything.

**Sixteen games tells you nothing, and it lies confidently.** The same comparison at three
sample sizes:

| Comparison | 16 games | 128 games | 512 games |
|---|---|---|---|
| +piece-square vs material only | 9-1-6 | 70-5-53 | overwhelming |
| +king safety vs +piece-square | 4-2-10 *(looks good)* | 28-39-61 | **104-145-263, −2.6σ** |

At 16 games king safety looked like a mild win. It is a real loss. A 512-game match takes
about 35 seconds — there is no excuse for a short one. `tests/match.c` is validated by having
a configuration play *itself*, which must come out exactly level, and does at every sample
size.

**Compare at equal time, not equal nodes** (§5.4). Every richer evaluation wins at equal
nodes; that is not the question a player is asking.

---

# Appendix A — Where the engine stands

Measured on a real C64 (NTSC jiffies, opening position), showing what the two Phase 5
optimisations bought:

| Budget | Depth | Before | After | Speedup |
|---|---|---|---|---|
| 400 | 2 | 1071 | 529 | 2.02x |
| 1600 | 3 | 2456 | 1299 | 1.89x |
| 6000 | 4 | 18095 | 8866 | 2.04x |

Note that 2x is **not** a ply. At an effective branching factor of about 6, a ply costs 6x.
Doubling the speed is worth having and does not make the engine visibly deeper.

Against the engine it replaced, at that engine's strongest setting: **6 wins, 0 losses**,
mating on ply 31 as White and ply 52 as Black.

Size on the C64 at `optspeed`: 34123 bytes total (CODE 25892, RODATA 3819, DATA 341,
BSS 4071); 31705 at `optsize`. Repetition detection accounts for 2882 of that — see §6.10.
**All seven targets link and fit**, the Apple II most narrowly at 1255 bytes spare; the
full table is in the Phase 5 notes of `doc/rework-log.md`.

# Appendix B — What is deliberately absent

**No opening book.** It competes with the tightest memory budget, and it would reduce test
coverage by making self-play games repeat their openings.

**No transposition table.** Different move orders often reach the same position; a hash table
of positions already searched avoids re-searching them. Affordable now that the attack
database is gone, and probably the best remaining value per byte. It needed incremental
position hashing first, and §6.10 has now built that — so what remains is the table itself,
and finding the RAM for it on the Apple II, which is the tightest target by a wide margin.

**Pawn structure** — doubled, isolated and passed pawns (§5.4). The one deferred term still
out. Its sibling came back as the endgame tables (§5.4a); this one needs a per-file pawn count
carried alongside the score, since it is a property of the whole pawn configuration rather than
of a piece on a square.

**No opening variety.** From the starting position the engine plays the same first move every
game, because the evaluation rates several openings exactly equal and ties break on generator
order. The fix is to choose randomly among moves within a few hundredths of a pawn of the best,
and it has to be switchable, since every measurement here depends on the search being
deterministic. What blocks it is not the choosing but the seed: `plat.h` exposes no clock, so
the entropy has to come from human input — menu choices, cursor keys, the squares of moves
played — which leaves the very first move after a cold boot deterministic.

**No pin set** (§6.9) — dropped on measurement, and the reasoning is recorded so it does not
get re-proposed.
