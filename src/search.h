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

// Exact-state switch keeps the old quiescence path for target A/B measurement.
#ifndef SEARCH_QUIESCE_HISTORY
#define SEARCH_QUIESCE_HISTORY	0
#endif
#ifndef SEARCH_RESTORE_UNMAKE
#define SEARCH_RESTORE_UNMAKE	1
#endif
// C1 candidate: score and place the best move in one pass for internal lists.
// Measured slower in its size-conscious form and too large for Atari's display-
// list page in the faster form; kept default off for reproduction only.
// Root must not place first - randomisation and previous-iteration priority
// rewrite scores afterwards and would change later tie order among equals.
#ifndef SEARCH_SCORE_FIRST
#define SEARCH_SCORE_FIRST	0
#endif

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
// Check evasions in quiescence, switchable so its cost can be measured against
// its absence.  Without it quiescence stands pat and looks only at captures
// even when the side to move is in check, which makes checkmate invisible to a
// depth 1 search - and level 1's 400 nodes rarely reach depth 2.
// The -D form exists for on-target A/B measurement: the tuning build cannot be
// used for that, because EVAL_TUNING makes every node dearer and the thing being
// priced is what a node costs.  Building the shipping configuration twice, once
// with -DSEARCH_CHECK_EVASION=0, is the only way to get the two numbers from the
// same compiler on the same machine
#ifdef EVAL_TUNING
extern char geSearchCheckEvasion;
#define SEARCH_CHECK_EVASION	geSearchCheckEvasion
#elif !defined(SEARCH_CHECK_EVASION)
#define SEARCH_CHECK_EVASION	1
#endif

/*-----------------------------------------------------------------------*/
// One extra ply when the side to move is in check (E5).  Bounded by
// SEARCH_MAX_PLY and the node budget — consecutive checks cannot escape
// those.  Default off until measured; suite may force it on
#ifndef SEARCH_CHECK_EXT
#define SEARCH_CHECK_EXT	0
#endif

/*-----------------------------------------------------------------------*/
// Opening randomisation, switchable the same way.  Note the direction: unlike
// every other switch here this one is a *feature* of the shipped game rather
// than a term being measured, so the 8-bit build has it permanently on and the
// tuning build is the one that can turn it off - which is what lets its cost
// be measured at all.
//
// It does nothing until search_SetSeed is called, which only the game does.
// Every harness in tests/ leaves the seed at zero and therefore plays exactly
// the games it played before this existed - the determinism every figure in
// doc/strength.md depends on is a property of nobody calling that function,
// not of a flag anyone has to remember to set
#ifdef EVAL_TUNING
extern char geSearchRandomOpening;
#define SEARCH_RANDOM_OPENING	geSearchRandomOpening
#else
#define SEARCH_RANDOM_OPENING	1
#endif

// How many of the engine's own moves are randomised.  Its moves, not plies -
// "my first five" is the same promise whichever colour it has
#define SEARCH_RANDOM_MOVES		5

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
// Start a game's opening randomisation from this seed, and restart the count
// of randomised moves.  Called once a game, by the game.  Zero means no
// randomisation - it is the LFSR's dead state, so it needs no special case,
// and it is the same state the test harnesses are in by never calling this
void search_SetSeed(char seed);

// The next byte from that generator, and whether a game has seeded it at all.
// cpu.c uses both for the opening table: unseeded means no table and no
// randomisation, which is the state every harness in tests/ runs in
char search_Random(void);
char search_Seeded(void);

/*-----------------------------------------------------------------------*/
// Is the side to move mated, stalemated or fine?  Returns OUTCOME_CHECKMATE,
// OUTCOME_STALEMATE or OUTCOME_OK.  With a real search this is just "are
// there legal moves, and am I in check" - no special-case machinery needed
char search_Outcome(char side);

#ifdef EVAL_TUNING
// Test-only entry to exercise quiescence return paths and prove its no-history
// subtree restores the parent position key and ring exactly.
char search_TestQuiesceState(char side, unsigned int budget, char exhaustArena);

// Full selection-order equivalence: fused score+first-place matches classic
// score-then-pickBest for every index, including a planted killer.
char search_TestOrderSequence(char side, char capturesOnly);
#endif

#endif //_SEARCH_H_
