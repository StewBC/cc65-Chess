/*
 *	selfplay.c
 *	cc65 Chess - test support
 *
 *	Plays the engine against itself through cpu_Play, which is the same path
 *	the game uses, and reports result, nodes and time.  This is the instrument
 *	Phase 4 tunes with.
 *
 *	Both sides are deterministic, so every game at a given skill is the same
 *	game.  Phase 4 needs opening variety before this can measure anything
 *	finer than "did the change break it".
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "undo.h"
#include "board.h"
#include "search.h"
#include "cpu.h"
#include "testutil.h"

int test_RunSelfPlay(int games, int maxPlies, int verbose)
{
	int failures = 0, skill;

	printf("selfplay: %d ply limit\n", maxPlies);
	printf("  %5s %6s %9s %10s  %s\n", "skill", "plies", "total(s)", "nodes/move", "result");

	for(skill = 0; skill < SEARCH_NUM_SKILLS; ++skill)
	{
		char side = SIDE_WHITE, outcome = OUTCOME_OK, lastMover = SIDE_WHITE;
		int ply;
		unsigned long nodes = 0;
		clock_t started;
		const char *result = "unfinished";
		t_searchResult probe;

		board_Init();
		undo_Init();
		gSkillLevel = skill;
		gUserMode = 0;

		started = clock();
		for(ply = 0; ply < maxPlies; ++ply)
		{
			lastMover = side;

			// same search the turn will run, so the node count is honest
			search_Best(side, gcSearchSkill[skill].m_depth,
			            gcSearchSkill[skill].m_nodes, &probe);
			nodes += probe.m_nodes;

			outcome = cpu_Play(side);

			if(KING != (geBoard[geKing[0]] & PIECE_DATA) ||
			   KING != (geBoard[geKing[1]] & PIECE_DATA))
			{
				printf("    skill %d ply %d: king tracker broken\n", skill, ply);
				++failures;
				break;
			}

			if(outcome >= OUTCOME_CHECKMATE)
				break;
			side = 1 - side;
		}
		started = clock() - started;

		switch(outcome)
		{
			case OUTCOME_CHECKMATE:
				result = lastMover == SIDE_WHITE ? "black mates" : "white mates";
			break;
			case OUTCOME_STALEMATE: result = "stalemate"; break;
			case OUTCOME_DRAW:      result = "draw (50 move)"; break;
		}

		printf("  %5d %6d %9.2f %10lu  %s\n", skill + 1, ply,
		       (double)started / CLOCKS_PER_SEC,
		       ply ? nodes / (unsigned long)ply : 0UL, result);
	}

	(void)games; (void)verbose;
	printf("  -> %d failing\n", failures);
	return failures;
}
