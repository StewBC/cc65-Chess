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
