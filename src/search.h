/*
 *	search.h
 *	cc65 Chess
 *
 *	Iterative-deepening negamax with alpha-beta, MVV-LVA and killer move
 *	ordering, and a quiescence search over captures.
 *
 *	The search is bounded by a node budget rather than a clock.  None of the
 *	eight platforms share a timer, and a node budget makes every one of them
 *	play the identical game - and lets an accelerated emulator simply finish
 *	sooner, with no code aware that anything changed.
 */

#ifndef _SEARCH_H_
#define _SEARCH_H_

#include "types.h"
#include "engine.h"

// Deepest the quiescence search may run past the main search
#define SEARCH_MAX_PLY		12

/*-----------------------------------------------------------------------*/
// Scoring a repeated position as a draw, switchable the same way and for the
// same reason as the evaluation terms: measuring a change means playing it
// against its own absence, and the two sides of that match have to live in
// one binary.  The tuning build can turn it off; the 8-bit build cannot, and
// pays neither a byte nor a test for the switch.
//
// Note what this does and does not isolate.  Both sides of such a match still
// carry the incremental hash in eng_Make and eng_Unmake, so it measures what
// detecting repetitions is worth, not what maintaining the hash costs.  That
// second number is a nodes/sec comparison against a build without the hash at
// all, and the two have to be put together before the change can be judged at
// equal time
#ifdef EVAL_TUNING
extern char geSearchRepetition;
#define SEARCH_REPETITION	geSearchRepetition
#else
#define SEARCH_REPETITION	1
#endif

/*-----------------------------------------------------------------------*/
typedef struct tag_searchResult
{
	t_engMove		m_move;			// best move found
	int				m_score;		// its score, from the searched side's view
	char			m_depth;		// deepest iteration actually completed
	char			m_haveMove;		// 0 when the side has no legal move at all
	unsigned int	m_nodes;		// nodes visited
} t_searchResult;

/*-----------------------------------------------------------------------*/
// The four skill levels, replacing gSkill's gWidth/gMaxLevel/gDeepThoughts.
// The depth cap is the primary weakener - a shallow but clean search makes a
// better beginner opponent than a deep one cut off mid-thought - and the node
// budget is the safety valve that keeps a move from taking all afternoon.
//
// Budgets are measured, not guessed: on a real C64 the search manages roughly
// 22 to 42 nodes/sec depending on how much of the time goes to quiescence
// (see tests/c64search.c).  Levels 3 and 4 are deliberately past what bare
// metal can do comfortably - they are where an accelerated emulator earns its
// keep.
typedef struct tag_searchSkill
{
	char			m_depth;
	unsigned int	m_nodes;
} t_searchSkill;

#define SEARCH_NUM_SKILLS	4

extern const t_searchSkill gcSearchSkill[SEARCH_NUM_SKILLS];

/*-----------------------------------------------------------------------*/
// Search "side" to at most maxDepth, stopping early if nodeBudget is spent.
// Iterative deepening means there is always a usable move from the last
// iteration that finished
void search_Best(char side, char maxDepth, unsigned int nodeBudget, t_searchResult *result);

/*-----------------------------------------------------------------------*/
// Is the side to move mated, stalemated or fine?  Returns OUTCOME_CHECKMATE,
// OUTCOME_STALEMATE or OUTCOME_OK.  With a real search this is just "are
// there legal moves, and am I in check" - no special-case machinery needed
char search_Outcome(char side);

#endif //_SEARCH_H_
