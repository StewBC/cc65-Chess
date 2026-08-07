/*
 *	match.c
 *	cc65 Chess - test support
 *
 *	The instrument Phase 4 tunes with.  Two configurations play each other from
 *	a set of openings, each opening twice with the colours swapped, and the
 *	score comes back as wins/losses/draws.
 *
 *	The opening set is the whole point.  Both sides are deterministic, so from
 *	the initial position they play the same game every time and a hundred games
 *	tell you exactly what one game tells you.  Sixteen games from eight
 *	different openings is a real, if small, sample.
 *
 *	How to read the result: a change is worth keeping if it wins clearly over
 *	the set.  With sixteen games a one game edge is noise - look for something
 *	like 10-4-2 or better before believing it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "undo.h"
#include "board.h"
#include "eval.h"
#include "search.h"
#include "testutil.h"

/*-----------------------------------------------------------------------*/
// The opening set is generated rather than written out: a few plies of random
// legal moves from the start, one fixed seed per opening, keeping only
// positions where neither side is already ahead.  That gives as much variety
// as is wanted, reproducibly, without a page of hand-typed FENs to get wrong.
#define NUM_OPENINGS	256
#define OPENING_PLIES	4

typedef struct tag_Position
{
	char	m_board[128];
	char	m_castle;
	char	m_ep;
	char	m_halfmove;
	char	m_king[2];
	char	m_side;
} t_Position;

static t_Position st_openings[NUM_OPENINGS];
static char sc_openingsBuilt;
static char sc_useEndgames;

/*-----------------------------------------------------------------------*/
static void savePosition(t_Position *pos, char side)
{
	memcpy(pos->m_board, geBoard, 128);
	pos->m_castle = geCastle;
	pos->m_ep = geEP;
	pos->m_halfmove = geHalfmove;
	pos->m_king[0] = geKing[0];
	pos->m_king[1] = geKing[1];
	pos->m_side = side;
}

static char loadPosition(const t_Position *pos)
{
	memcpy(geBoard, pos->m_board, 128);
	geCastle = pos->m_castle;
	geEP = pos->m_ep;
	geHalfmove = pos->m_halfmove;
	geKing[0] = pos->m_king[0];
	geKing[1] = pos->m_king[1];
	return pos->m_side;
}

/*-----------------------------------------------------------------------*/
// plies: how deep to wander before saving.  A handful of plies gives opening
// positions; sixty-odd with captures allowed gives thinned-out endings, which
// is the only way to measure a term that only applies once the queens are off
static void buildPositions(int plies, char allowCaptures)
{
	int i;

	for(i = 0; i < NUM_OPENINGS; ++i)
	{
		char side = SIDE_WHITE;
		int ply;

		srand(1000 + i);
		eng_SetStartPosition();

		for(ply = 0; ply < plies; ++ply)
		{
			t_engMove moves[ENG_MAX_MOVES];
			t_engUndo undo;
			char count = eng_GenMoves(side, moves, ENG_MAX_MOVES);
			char tries, done = 0;

			for(tries = 0; tries < count * 2 && !done; ++tries)
			{
				char pick = rand() % count;

				eng_Make(&moves[pick], &undo);
				if(eng_IsAttacked(geKing[side], 1 - side) ||
				   (!allowCaptures && NONE != (undo.m_captured & PIECE_DATA)))
					eng_Unmake(&moves[pick], &undo);
				else
					done = 1;
			}
			if(!done)
				break;
			side = 1 - side;
		}

		savePosition(&st_openings[i], side);
	}
}

/*-----------------------------------------------------------------------*/
static void buildOpenings(void)
{
	if(sc_openingsBuilt != 1)
	{
		buildPositions(OPENING_PLIES, 0);
		sc_openingsBuilt = 1;
	}
}

static void buildEndgames(void)
{
	if(sc_openingsBuilt != 2)
	{
		buildPositions(70, 1);
		sc_openingsBuilt = 2;
	}
}

/*-----------------------------------------------------------------------*/
typedef struct tag_Config
{
	const char		*m_name;
	char			 m_terms;		// EVAL_* mask
	char			 m_depth;
	unsigned int	 m_nodes;
} t_Config;

/*-----------------------------------------------------------------------*/
// +1 if "a" won, -1 if "b" won, 0 for a draw or an unfinished game
static int si_drawFifty, si_drawStale, si_drawUnfinished;

static int playGame(const t_Position *opening, const t_Config *a, const t_Config *b,
                    char aSide, int maxPlies, unsigned long *nodesOut)
{
	char side = loadPosition(opening);
	int ply;

	undo_Init();
	board_SyncDisplay();

	for(ply = 0; ply < maxPlies; ++ply)
	{
		const t_Config *cfg = (side == aSide) ? a : b;
		t_searchResult result;
		char outcome = search_Outcome(side);

		if(OUTCOME_CHECKMATE == outcome)
			return (side == aSide) ? -1 : 1;		// side to move is mated
		if(OUTCOME_STALEMATE == outcome) { ++si_drawStale; return 0; }
		if(geHalfmove >= 100)            { ++si_drawFifty; return 0; }

		geEvalTerms = cfg->m_terms;
		search_Best(side, cfg->m_depth, cfg->m_nodes, &result);
		*nodesOut += result.m_nodes;

		if(!result.m_haveMove)
		{
			++si_drawStale;
			return 0;
		}

		board_ApplyMove(&result.m_move, side);
		side = 1 - side;
	}

	++si_drawUnfinished;
	return 0;
}

/*-----------------------------------------------------------------------*/
static int runMatch(const t_Config *a, const t_Config *b, int maxPlies, int verbose)
{
	int i, wins = 0, losses = 0, draws = 0;
	unsigned long nodes = 0;
	clock_t started = clock();

	if(sc_useEndgames) buildEndgames(); else buildOpenings();
	si_drawFifty = si_drawStale = si_drawUnfinished = 0;
	printf("  %s  vs  %s\n", a->m_name, b->m_name);

	for(i = 0; i < NUM_OPENINGS; ++i)
	{
		int g;

		for(g = 0; g < 2; ++g)
		{
			// g 0: a is white.  g 1: a is black, same opening
			int outcome = playGame(&st_openings[i], a, b,
			                       g ? SIDE_BLACK : SIDE_WHITE, maxPlies, &nodes);

			if(outcome > 0) ++wins;
			else if(outcome < 0) ++losses;
			else ++draws;

			if(verbose)
				printf("    opening %d, %s as %s: %s\n", i, a->m_name,
				       g ? "black" : "white",
				       outcome > 0 ? "win" : outcome < 0 ? "loss" : "draw");
		}
	}

	printf("    %d-%d-%d (W-L-D) over %d games, %lu nodes, %.1fs\n",
	       wins, losses, draws, NUM_OPENINGS * 2, nodes,
	       (double)(clock() - started) / CLOCKS_PER_SEC);
	if(draws)
		printf("    draws: %d fifty-move, %d stalemate, %d hit the %d ply limit\n",
		       si_drawFifty, si_drawStale, si_drawUnfinished, maxPlies);

	geEvalTerms = EVAL_ALL;
	return wins - losses;
}

/*-----------------------------------------------------------------------*/
// A configuration playing itself must come out level: the same eight games
// twice with the colours swapped, so every result has its mirror.  If this is
// not balanced the harness is measuring something other than the change
int test_RunMatchSanity(int verbose)
{
	t_Config same = { "all-terms", EVAL_ALL, 3, 2000 };
	int edge;

	printf("match sanity: a configuration against itself\n");
	edge = runMatch(&same, &same, 240, verbose);

	if(edge != 0)
	{
		printf("  -> FAIL: self-play is not balanced (edge %+d)\n", edge);
		return 1;
	}
	printf("  -> ok, balanced\n");
	return 0;
}

/*-----------------------------------------------------------------------*/
// The evaluation terms are only free if nodes are free, and they are not.
// Measured on a real C64 (tests/c64search.c), the pawn-structure term makes
// every node about 1.35x more expensive, so in the time the simpler evaluation
// searches 2000 nodes the richer one searches about 1480.  That is the
// comparison the player actually experiences
// The endgame king table can only show up in an endgame, and games started
// from an opening rarely get there before the ply limit
// Each skill level must actually beat the one below it, or the ladder is a lie
int test_RunMatchLadder(int verbose)
{
	char level;

	printf("match: does each skill level beat the one below it?\n");
	for(level = 1; level < SEARCH_NUM_SKILLS; ++level)
	{
		char nameHi[24], nameLo[24];
		t_Config hi, lo;

		sprintf(nameHi, "level %d", level + 1);
		sprintf(nameLo, "level %d", level);
		hi.m_name = nameHi; hi.m_terms = EVAL_ALL;
		hi.m_depth = gcSearchSkill[level].m_depth;
		hi.m_nodes = gcSearchSkill[level].m_nodes;
		lo.m_name = nameLo; lo.m_terms = EVAL_ALL;
		lo.m_depth = gcSearchSkill[level-1].m_depth;
		lo.m_nodes = gcSearchSkill[level-1].m_nodes;

		runMatch(&hi, &lo, 240, verbose);
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunMatchEndgame(int verbose)
{
	t_Config phase = { "phase-aware king", EVAL_ALL, 4, 3000 };
	t_Config flat  = { "one king table",   EVAL_MATERIAL|EVAL_PST, 4, 3000 };

	printf("match: the endgame king table, measured in actual endgames\n");
	sc_useEndgames = 1;
	runMatch(&phase, &flat, 240, verbose);
	sc_useEndgames = 0;
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunMatchEqualTime(int verbose)
{
	t_Config rich = { "pawn struct, 1480 nodes", EVAL_ALL, 3, 1480 };
	t_Config lean = { "material+pst, 2000 nodes", EVAL_MATERIAL|EVAL_PST, 3, 2000 };

	printf("match: pawn structure at equal TIME rather than equal nodes\n");
	runMatch(&rich, &lean, 240, verbose);
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunMatchTerms(int verbose)
{
	t_Config base   = { "material only",  EVAL_MATERIAL, 3, 2000 };
	t_Config pst    = { "+pst",           EVAL_MATERIAL|EVAL_PST, 3, 2000 };
	t_Config all    = { "everything",  EVAL_ALL, 3, 2000 };

	// each term measured against what it is being added to, one at a time
	printf("match: which evaluation terms actually earn their bytes?\n");
	runMatch(&pst,   &base, 240, verbose);
	runMatch(&all,   &pst,  240, verbose);
	return 0;
}

/*-----------------------------------------------------------------------*/
// Depth is the one thing that should be unambiguously worth having.  If a
// deeper search does not beat a shallower one, something is wrong with the
// search rather than with the evaluation
int test_RunMatchDepth(int verbose)
{
	t_Config deep    = { "depth 4", EVAL_ALL, 4, 20000 };
	t_Config shallow = { "depth 2", EVAL_ALL, 2, 20000 };

	printf("match: does searching deeper actually win?\n");
	runMatch(&deep, &shallow, 240, verbose);
	return 0;
}
