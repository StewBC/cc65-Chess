/*
 *	eval.h
 *	cc65 Chess
 *
 *	Position evaluation.  This is a pure function of the board: give it a
 *	position and it says how good that position is, in hundredths of a pawn,
 *	from the point of view of the side asked about.
 *
 *	That is the whole difference from the old cpu.c, which scored how much it
 *	wanted to make a *move*.  Move desirability cannot be compared or negated
 *	across plies, so it cannot be searched.  A position value can.
 *
 *	It is still a pure function of the position, but it is no longer *computed*
 *	that way.  geEvalScore is a running total kept up to date by eng_Make and
 *	eng_Unmake, so asking for the score is a read rather than a walk over 64
 *	squares.  See eval_MoveDelta for the one rule that keeps that honest.
 */

#ifndef _EVAL_H_
#define _EVAL_H_

#include "types.h"
#include "engine.h"

// Scores comfortably inside a 16-bit int: a whole side's material is about
// 4000, so there is plenty of room below the mate scores
#define EVAL_INFINITY	30000
#define EVAL_MATE		29000

// A mate found at "ply" plies from the root.  Deducting the ply makes a mate
// in two score higher than a mate in four, so the search prefers the quicker
// kill and, when losing, the longest resistance
#define EVAL_MATE_IN(ply)	(EVAL_MATE - (ply))

extern const int gcPieceValue[PAWN+1];

/*-----------------------------------------------------------------------*/
// Evaluation terms, so one build can play one set against another.  The test
// build defines EVAL_TUNING and can switch terms per side; the 8-bit build
// does not, and EVAL_HAS compiles away to nothing there - the target pays
// neither the byte nor the test
#define EVAL_MATERIAL		SET_BIT(0)
#define EVAL_PST			SET_BIT(1)
#define EVAL_PAWNSTRUCT		SET_BIT(3)
#define EVAL_ENDGAME		SET_BIT(4)
#define EVAL_MATEDRIVE		SET_BIT(5)
#define EVAL_QUEENHOME		SET_BIT(6)
#define EVAL_QUEENOUT		SET_BIT(7)
#define EVAL_ALL			(EVAL_MATERIAL|EVAL_PST|EVAL_PAWNSTRUCT|EVAL_ENDGAME|EVAL_MATEDRIVE)

// SET_BIT(6), EVAL_QUEENHOME, is deliberately **not** in EVAL_ALL, and neither
// is SET_BIT(7).  Every other bit turns off something the shipping engine does;
// these two turn candidates on, so off has to mean "what ships".  Both swap one
// number in the queen table - d1, which the shipped table scores at -5 while
// the middle scores +5, so the evaluation pays the queen ten centipawns to
// leave home (doc/strength.md §5.2a).
//
// EVAL_QUEENHOME sets that differential to zero: d1 becomes +5.
// EVAL_QUEENOUT triples it in the *other* direction: d1 becomes -15.
//
// The second one is not a candidate anybody would ship.  It is there because
// the first measured as nothing, and a change that measures as nothing has two
// explanations - the mechanism is not real, or the dose was too small.  Making
// the incentive larger separates them: if the square matters at all, -15 should
// be measurably worse than -5, and if it is not then this square is inert at
// this magnitude and §5.2a's mechanism is not where the queen's cost comes from

// Two terms were built, measured and taken out again; see the Phase 4 notes.
//
// SET_BIT(2), king safety by counting pawns in front of the king: -2.6 sigma
// over 512 games.  A genuine loss, not a wash.  A term based on how many enemy
// pieces bear on the squares round the king might still work - counting the
// pawn shield does not.
//
// SET_BIT(3), EVAL_PAWNSTRUCT, was the Phase 4 doubled/isolated/passed bundle:
// +2.0 sigma at equal nodes, then 1.35x dearer per node on a real C64 and
// +0.6 sigma at equal time - nothing - for 735 bytes.  That price was full-board
// leaf evaluation.  Phase E1 rebuilds doubled and isolated from per-file pawn
// counts updated on make/unmake; passed pawns stay a separate candidate.
//
// A phase-aware endgame king table: +1.9 sigma pooled over 1024 games, and
// 1.28x dearer per node for the same reason.  Working out the phase from a
// char count rather than an int sum made no difference at all - the cost is
// doing anything at all per piece, 32 times, at every node.
//
// Both were blocked on the same thing: the evaluation was recomputed from
// scratch at every node, so anything added to it was paid for 20000 times a
// move.  Phase 5 made it incremental, which is what unblocked them.
//
// EVAL_ENDGAME is the one that came back, and the interesting part is what it
// took.  Reinstating the king table alone measured +15 Elo at 1.55 sigma and
// did not improve conversion at all - the number it was built to move.  The
// reason was the pawns: the middlegame table pays 50 for a pawn on the seventh
// and 5 for one at home, so marching a pawn the length of the board earns 45,
// against the 800 that promoting is worth from beyond the horizon.  The king
// had somewhere to go and the pawns had no reason to move.
//
// With a steep endgame pawn table alongside it: +44 Elo at equal nodes, +30 at
// equal time charged the C64's 9%, and conversion from 69% to 78%.  The two
// terms are worth far more together than the king was alone.
//
// SET_BIT(5), EVAL_MATEDRIVE, is the third one and the same story a layer on.
// The endgame king table sends *both* kings to the middle, because it is a
// per-piece table and cannot tell which of them is being mated.  So with bare
// kings there was no gradient at all: every rook move scored the same, and the
// engine wandered until the fifty-move rule drew a game it had already won.
// This is the one term here that is not a property of a piece on a square - it
// takes both kings - so it is computed at eval time out of geKing rather than
// carried.  That is affordable for exactly one reason: it cannot fire outside
// the endgame, which is where nodes are cheapest.  See eval.c.

#ifdef EVAL_TUNING
extern char geEvalTerms;
#define EVAL_HAS(term)		(geEvalTerms & (term))
#else
#define EVAL_HAS(term)		1
#endif

/*-----------------------------------------------------------------------*/
// The mate drive, switchable at compile time as well as through geEvalTerms.
//
// This exists for the same reason SEARCH_CHECK_EVASION's -D form does, and the
// reason is worth repeating rather than cross-referencing: **the tuning build
// cannot price a node**, because EVAL_TUNING makes every node dearer and what
// is being measured is exactly what a node costs.  Building the shipping
// configuration twice, once with -DEVAL_MATEDRIVE_ON=0, is the only way to get
// two numbers from the same compiler on the same machine.
//
// EVAL_HAS is still consulted first, so the tuning build's switch keeps
// working for A/B match play; this only removes the term from a shipping build
#ifndef EVAL_MATEDRIVE_ON
#define EVAL_MATEDRIVE_ON	1
#endif

/*-----------------------------------------------------------------------*/
// Incremental pawn structure: doubled and isolated only.  Same dual-switch
// shape as the mate drive - the tuning build can clear EVAL_PAWNSTRUCT for
// A/B, and a shipping build with -DEVAL_PAWNSTRUCT_ON=0 is the only honest
// way to price the node on a 6502.
#ifndef EVAL_PAWNSTRUCT_ON
#define EVAL_PAWNSTRUCT_ON	1
#endif

/*-----------------------------------------------------------------------*/
// The running score, always from white's point of view.  Nothing outside
// eval.c and the two make/unmake functions should write it
extern int geEvalScore;

#if EVAL_PAWNSTRUCT_ON
/*-----------------------------------------------------------------------*/
// Last structure score computed by eval_Position.  Exposed for tests; not a
// running total - it is rebuilt from the board on every evaluation
extern int gePawnStruct;
#endif

/*-----------------------------------------------------------------------*/
// Non-pawn material left on the board, both sides, carried by make/unmake the
// same way the score is.  It decides how far into the endgame the position is,
// which is what the king table switches on
extern int gePhase;
int eval_PhaseDelta(const t_engMove *move, char piece, char captured);

/*-----------------------------------------------------------------------*/
// How much more the position is worth once the endgame tables apply.  Carried
// by make/unmake like the score, and blended in by eval_Position according to
// the phase.  Holding the difference rather than a second full score is what
// keeps this to one extra delta per move and nothing per node
extern int geEvalEnd;
int eval_EndDelta(const t_engMove *move, char piece, char captured);

/*-----------------------------------------------------------------------*/
// Position score from "side"'s point of view; positive is good for side.  Now
// just a read of geEvalScore, negated for black
int eval_Position(char side);

/*-----------------------------------------------------------------------*/
// Recompute geEvalScore from the board.  Needed after anything that puts
// pieces down without going through eng_Make - setting up a position, parsing
// a FEN, or switching evaluation terms in the tuning build.  search_Best calls
// it once per move, so the search can never inherit a stale total
void eval_Refresh(void);

/*-----------------------------------------------------------------------*/
// What "move" does to geEvalScore, where "piece" is the mover as it stood
// *before* the move and "captured" is what it took (NONE if nothing).
//
// The one rule: eng_Make adds this and eng_Unmake subtracts the very same
// call.  Both pass identical arguments and the function reads no board state,
// so unmake undoes make exactly, by construction rather than by care.  Any
// term added here has to obey that - it must be derivable from the move alone
int eval_MoveDelta(const t_engMove *move, char piece, char captured);

// Hundredths of a pawn for the doubled/isolated term.  Powers of two so the
// 6502 scores with shifts; exposed so tests can name the doses they expect
#define EVAL_PAWN_DOUBLED	(-8)
#define EVAL_PAWN_ISOLATED	(-16)

#endif //_EVAL_H_
