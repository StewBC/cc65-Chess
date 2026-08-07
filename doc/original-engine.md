# How the original engine worked

The engine described in `doc/engine.md` replaced an earlier one. This document is about that
earlier one: what it did, why its design is a reasonable place to arrive at, and precisely
where it went wrong.

It is here because the failure is more instructive than the fix. The original is not bad
code. It is clean, commented, and it does something genuinely hard correctly. It is weak at
chess for two specific structural reasons, and both of them are traps that are well known
inside computer chess and close to invisible from outside it. Seeing them named next to the
code that contains them is worth more than reading a description of the right answer.

The code is at the tag `v1-original-engine`:

```bash
git show v1-original-engine:src/cpu.c      # the AI, 713 lines
git show v1-original-engine:src/board.c    # movegen and the attack database, 802 lines
git checkout v1-original-engine            # or just go and look
```

---

## 1. Start with what it got right

**The move generator was correct.** That is not faint praise — it is the part most likely to
be subtly wrong, and it is the part everything else depends on.

Before any of it was replaced, the original generator was measured against perft, the
standard test for a move generator: count the leaf nodes of the game tree to a fixed depth
and compare against published reference values. A single wrongly generated or wrongly
rejected move anywhere changes the count. The original matched the reference **exactly to
depth 5** on the initial position and on the third standard test position, and to depth 3 on
"Kiwipete" — the position specifically constructed to catch castling and en passant
mistakes.

There was exactly one divergence in the whole exercise, and it had a single cause: the
generator never produced an **under-promotion**. A pawn reaching the back rank was one move,
and the piece it became was chosen later — a queen for the AI, a menu prompt for a human. So
every promoting move counted 1 where the reference counted 4. That is a design choice
showing up in a test, not a bug in the rules.

This is why the rework could *port* the movement rules rather than rederive them. The
castling conditions, the en passant window, the pawn double-step — all of that was already
right.

**The attack database is a good idea that earns its place.** More on it below, because it is
also the centre of the performance problem, but it is worth being clear about the order of
events: the program's most distinctive feature is a visualizer that shows you, on the board,
which squares each side attacks and defends. The database exists to make that display
possible, and for that purpose it is exactly the right structure.

---

## 2. The attack database

`gAttackBoard` holds, for every square and each side, a count of attackers followed by the
list of squares they come from:

```c
#define ATTACK_WIDTH  (2 + 2 * NUM_PIECES_SIDE)   // 34
char gAttackBoard[8][8][ATTACK_WIDTH];            // 2176 bytes
int  giAttackBoardOffset[64][2];                  //  256 bytes
```

2,432 bytes on a machine with a few kilobytes to spare — a real investment, made
deliberately, for the feature the program is actually about.

It is filled by `board_PlacePieceAttacks`, which walks all 64 squares, generates every move
for every piece it finds, and records each destination as an attack:

```c
for(i = 0; i < 64; ++i) {
    piece = gpChessBoard[i];
    if(piece != NONE) {
        board_GeneratePossibleMoves(i, 1);
        for(j = 0; j < gNumMoves; ++j) {
            int offset = giAttackBoardOffset[gPossibleMoves[j]][color];
            gpAttackBoard[offset + 1 + gpAttackBoard[offset]] = i;
            ++gpAttackBoard[offset];
        }
    }
}
```

As a way to fill a display, this is fine. It runs when the board changes and the answer is
then available for free, 64 squares at a time, which is exactly what the overlay wants.

### The trap

Once you have that structure, one more use for it is irresistible, and it is the correct
next thought for anyone building this: **"is this move legal" is the same question as "is my
king attacked", and I already have a table of exactly that.**

So `board_ProcessAction` makes the move, rebuilds the database, and looks:

```c
gpChessBoard[gTile[1]] = gpChessBoard[gTile[0]];
gpChessBoard[gTile[0]] = NONE;

board_PlacePieceAttacks();          // rebuild all 64 squares

if(gpAttackBoard[giAttackBoardOffset[kingTile][1 - gColor[0]]]) {
    /* illegal - unwind everything */
}
```

The lookup is one array read. The *rebuild that makes the lookup meaningful* is a full-board
attack regeneration: 64 squares, a move generation for every piece on each, every
destination written into the table.

**Testing one move for legality therefore costs a complete regeneration of the world.** And
legality testing is the single most frequent operation in a chess engine — it happens for
every move, at every node, at every depth.

This is the whole performance story. The structure is a cache, and the expensive part of a
cache is keeping it valid. Rebuilding it in full after every trial move means it is not
really a cache at all; it is a very expensive query wearing one's clothing.

The replacement does not make this faster. It stops asking. `eng_IsAttacked` walks outwards
from one square — about forty array reads — and answers the only question that was ever
being asked, without materialising the other 63 squares' worth of answer that nobody wanted.
The visualizer still gets its data, computed on demand for the one square the cursor is on.

That change alone made the engine **32× faster** at the same work: Kiwipete to depth 5 went
from 139 seconds to 4.3.

### The knock-on cost

Because the database is expensive to maintain, an escape hatch appeared: `gDeepThoughts`, a
flag that switches it off during search. The two easier skill levels run with it off, which
means the search reasons about positions whose attack data is stale — so the moves it
considers may not be legal, and the moves it rejects may not be illegal. Fast and wrong,
against slow and right, with no third option.

The replacement retires the flag entirely, not by choosing a side but by removing the reason
the choice existed. Once legality is a ray-cast, accurate and fast stop being opposed.

---

## 3. The scoring: move desirability

Here is the second structural decision, and it is subtler than the first.

The engine scores **how much it wants to make a move**, in two halves. `cpu_SourceScore` asks
what is true about a piece where it stands:

```c
// If this piece is under attack increase chance to move, else decrease
if(gpAttackBoard[giAttackBoardOffset[position][other]])
    score += value;
else
    score -= 1;
```

and `cpu_DestScore` asks what would be true about it after the move:

```c
// If this piece will be under attack decrease chance to move
if(gpAttackBoard[giAttackBoardOffset[destination][other]])
    score -= value;
```

Read them on their own and they are sensible. A hanging piece should want to move. A square
covered by the enemy is a worse place to stand than one that is not. Supporting a defended
piece is worth something. There is real chess knowledge in these two functions, and against a
beginner it produces recognisable, purposeful-looking moves.

The trouble is what the number *is*.

**These scores are opinions about an action, not properties of a position.** "+4, this piece
would quite like to move" is not a quantity that exists on the board. And that matters the
moment you try to look ahead, because looking ahead means combining scores across plies —
which requires the one property a move-desirability score does not have.

### The symmetry that makes lookahead work

A *position value* can be negated. If a position is worth +150 to White, it is worth −150 to
Black — not by convention but by definition, because it is one fact viewed from two sides.
That symmetry is the entire mechanism that lets a search pass scores up a tree: my best reply
to your move is the one that minimises the value of the position I hand back to you, which is
the same as maximising it for me.

Move desirability has no such symmetry. "White wants to play Nf3 (+4)" and "Black wants to
play Nc6 (+3)" are two unrelated opinions about two different actions. Subtracting one from
the other does not describe anything.

The engine subtracts them anyway. `cpu_ScorePieceSubTree` accumulates the scores down the
line with an alternating sign:

```c
score = cpu_FindBestOpponentMove(1 - side, &from, &to);
score = (score * sign);
if(level < gMaxLevel)
    score += cpu_ScorePieceSubTree(level + 1, 1 - side, sign ^ 0xfe, from, to);
```

This is the shape of negamax, and the shape is right. The quantity flowing through it is
not. Deepening a search over numbers that do not mean anything does not produce better
play — which is why the deeper skill levels in the original are not much stronger than the
shallow ones, and why *no amount* of extra search would have fixed it.

**This is the more important of the two problems.** The attack database made the engine slow.
The scoring made depth not help.

---

## 4. The search: a branching factor of one

The third piece follows from the second, and it is the one that looks most like minimax
without being it.

**At the root, only one move per piece is considered.** `cpu_ScorePieceMoves` walks the ≤16
pieces on the side to move; for each, it scores every destination, sorts them, and keeps the
single best — one entry per piece:

```c
sts_pieceScores[i].m_dest   = sts_moveScore[j].m_dest;
sts_pieceScores[i].m_score += sts_moveScore[j].m_score;
```

Those 16 candidates are then sorted, and `gWidth` of them get searched. In a middlegame there
are typically 30 to 40 legal moves. So more than half are never considered at all — and,
more sharply, a move can only be considered if it is the *best move for its own piece*
according to a static heuristic. If the winning move is the second-best thing that knight
could do, it is invisible.

**Below the root, the branching factor is one.** `cpu_ScorePieceSubTree` makes a move and
then asks `cpu_FindBestOpponentMove` for the opponent's single top-scoring reply — and
recurses on that one move only.

So the "tree" is a set of straight lines: one predicted continuation per root move. Nothing
is searched in the sense of being explored; a static heuristic picks a line and the engine
walks down it, adding up numbers.

This is why it never sees a refutation. Alpha-beta's whole value is that it examines the
replies it *does not* expect, and prunes only when it has proved it can stop. Following the
heuristic's favourite reply assumes the heuristic was right — and if the heuristic were
reliable enough for that, you would not need a search.

The skill settings make the shape plain. `gSkill` holds `(gWidth, gMaxLevel, gDeepThoughts)`:

| Menu | Root moves | Levels deep | Accurate legality |
|---|---|---|---|
| Very Easy | 1 | 0 | no |
| Easy | 16 | 1 | no |
| Harder | 16 | 2 | yes |
| Very Hard | 16 | 3 | yes |

Even at the strongest setting: 16 root moves out of 30–40, four plies, one line each.

---

## 5. What followed from all this

Two design choices propagate a long way. Both of the following are consequences rather than
independent mistakes.

**Checkmate needed its own machinery.** If legality is expensive and the search does not
enumerate legal moves, you cannot detect mate the natural way. So mate detection became a
special case: `board_CheckForMate` counts the king's attackers, asks whether the king has a
flight square, and for a single attacker calls `board_CheckLineAttack` to work out whether
the attacker can be captured or the line blocked. That in turn needs `board_UpdateAttackGrid`
to *temporarily add* entries to the database — squares behind the king that the king cannot
retreat to — and `si_fixupTable` to remember them so they can be taken out again:

```c
cleanup:
    while(sc_numFixes) {
        --sc_numFixes;
        --gpAttackBoard[si_fixupTable[sc_numFixes]];
    }
```

Around 200 lines, mutating a shared structure and repairing it afterwards, which is a
classic source of bugs that only appear in rare positions. In the replacement this is nine
lines, and it is not machinery at all: *no legal moves, and in check* is mate; *no legal
moves, and not in check* is stalemate. It falls out of having a legal move generator, for
free.

**Promotion happened during the move, not as part of it.** `board_ProcessAction` decides
what the pawn becomes at the moment the move is made — always a queen for the AI. This is
the under-promotion gap from §1. It is also the reason a move could not be a self-contained
value that make and unmake fully describe, which is what a search needs.

**A positional-initialiser hazard in the piece values.** Worth including because it is a pure
C trap with nothing to do with chess. The table is initialised by position, but its comments
run in a different order from the enum they are labelling:

```c
char gPieceValues[PAWN+1] = {
        (0),    // NONE
    2+(3*5),    // ROOK,
    2+(3*3),    // KNIGHT,
    2+(3*3),    // BISHOP,
    2+(3*10),   // KING,      <-- index 4 is QUEEN
    2+(3*9),    // QUEEN,     <-- index 5 is KING
    2+(3*1),    // PAWN,
};
```

The enum is `NONE, ROOK, KNIGHT, BISHOP, QUEEN, KING, PAWN`. So the value meant for the king
(32) lands on the queen, and the queen's (29) lands on the king — and capturing a king scores
*less* than capturing a queen. It matters least where you would expect it to matter most:
`cpu_ScorePieceSubTree` returns `gPieceValues[KING]` when a king is capturable, which is a
position that should be unreachable, and is only reachable at all in the two skill levels
that run with stale legality data.

Designated initialisers (`[QUEEN] = ...`) would have made it impossible. So would a table
that checks itself once at startup.

---

## 6. What it cost, and what changed

The two engines played each other. At the original's strongest setting: **6 wins, 0 losses,
0 draws**, mated on ply 31 as White and ply 52 as Black.

The replacement is also *smaller*. Roughly 750 lines came out against 400 going in, and on
the C64 at `optspeed`:

| Segment | Original | Replacement | Change |
|---|---|---|---|
| CODE | 25079 | 24020 | **−1059** |
| RODATA | 1798 | 2235 | +437 (piece-square tables) |
| BSS | 4318 | 3808 | **−510** |
| **Total** | **31543** | **30407** | **−1136** |

An 0x88 board, make/unmake, alpha-beta, quiescence and move ordering together are **1,136
bytes smaller** than the machinery they replaced, and use 514 bytes less RAM. Most of the
saving is the attack database no longer existing as a stored structure.

That is the detail worth sitting with. The rewrite was not a matter of spending more
resources to get more strength. The original was paying, in both bytes and time, for
structures whose cost was mostly not buying anything.

---

## 7. What generalises

**A cache's cost is invalidation, and rebuilding is not invalidation.** The attack database
was correct at every moment — by being regenerated from scratch whenever anything moved.
Correct and ruinous. When a derived structure is rebuilt more often than it is read, the
structure is the problem.

**Ask whether your number is a property or an opinion.** Anything a search accumulates has to
be a property of the *position*, because propagating it up a tree means negating it for the
other side. A score attached to an action cannot be negated, because the other side is not
considering that action. This is the single most consequential idea in the whole rewrite and
it costs nothing to apply — it is a question you ask before writing the function.

**A structure that looks like the right algorithm may not be it.** `cpu_ScorePieceSubTree`
has the recursion, the alternating sign, the accumulation — everything about minimax except
examining more than one move. It is genuinely hard to see, and the giveaway is not in the
code's shape but in its behaviour: depth was not buying strength. **When more of something
does not help, suspect that the thing is not what you think it is.**

**The specialty-knowledge asymmetry is real.** Nothing above requires unusual cleverness to
avoid. It requires knowing which two or three things matter in this particular domain — that
evaluation must be a position value, that legality must be cheap because it is the innermost
loop. Those facts are unremarkable to anyone who has written a chess engine and are not
deducible from general programming skill. A program that grows from "can I generate legal
moves?" into an engine will pass straight through both traps, because at each individual step
the next decision looks obviously right.

Which it was. The attack database *is* the right structure for the display it was built for.
Reusing it for legality *is* the natural next thought. Scoring how good a move looks *is* how
a person describes choosing a move. Every step is locally sensible; the destination is a
program that cannot be made stronger by searching deeper.

---

## Where to look

| File at `v1-original-engine` | What is in it |
|---|---|
| `src/cpu.c` | The AI. `cpu_Play` is the entry point; read `cpu_ScorePieceMoves`, then `cpu_ScorePieceSubTree`. |
| `src/board.c` | Move generation, the attack database, `board_ProcessAction`, and the mate machinery. |
| `src/globals.c` | `gSkill` and `gPieceValues`. |

`doc/engine.md` describes what replaced it, and is organised so the same questions get
answered in the same order.
