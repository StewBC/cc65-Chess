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
#define EVAL_ALL			(EVAL_MATERIAL|EVAL_PST)

// Two terms were built, measured and taken out again; see the Phase 4 notes.
//
// SET_BIT(2), king safety by counting pawns in front of the king: -2.6 sigma
// over 512 games.  A genuine loss, not a wash.  A term based on how many enemy
// pieces bear on the squares round the king might still work - counting the
// pawn shield does not.
//
// SET_BIT(3), pawn structure (doubled, isolated, passed): +2.0 sigma at equal
// node counts, but it made every node 1.35x dearer on a real C64, and at equal
// *time* that came to +0.6 sigma - no measurable difference - for 735 bytes.
//
// A phase-aware endgame king table: +1.9 sigma pooled over 1024 games, and
// 1.28x dearer per node for the same reason.  Working out the phase from a
// char count rather than an int sum made no difference at all - the cost is
// doing anything at all per piece, 32 times, at every node.
//
// Both are good terms.  Both were blocked on the same thing: the evaluation was
// recomputed from scratch at every node, so anything added to it was paid for
// 20000 times a move.  Phase 5 made it incremental, which is what unblocks
// them - a term that can be folded into eval_MoveDelta is now paid once per
// move made rather than once per node, so their measured gains should stand at
// equal *time* and not just at equal nodes.  Reinstate them one at a time, each
// behind a 512-game match, exactly as before.

#ifdef EVAL_TUNING
extern char geEvalTerms;
#define EVAL_HAS(term)		(geEvalTerms & (term))
#else
#define EVAL_HAS(term)		1
#endif

/*-----------------------------------------------------------------------*/
// The running score, always from white's point of view.  Nothing outside
// eval.c and the two make/unmake functions should write it
extern int geEvalScore;

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

#endif //_EVAL_H_
