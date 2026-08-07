/*
 *	budget.c
 *	cc65 Chess - test support
 *
 *	How many nodes does it actually cost to *complete* each depth, as a game
 *	goes on?
 *
 *	That is the question the skill table has to be set from, and it is the one
 *	the old table was not set from.  A budget that lands just short of finishing
 *	an iteration is the worst of both worlds: the whole budget is spent and the
 *	abandoned iteration is thrown away, so the engine pays full price and plays
 *	the shallower move anyway.  Level 1 was sitting exactly there - 14 seconds a
 *	move on a C64 to play one ply.
 *
 *	Node counts are platform independent, because the search is deterministic:
 *	whatever this reports natively is exactly what the 6502 will do.  Only the
 *	seconds have to be measured on the target.
 */

#include <stdio.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"
#include "testutil.h"

#define BUDGET_MAX_DEPTH	5
#define BUDGET_PLIES		24
#define BUDGET_CEILING		60000u	// fits cc65's 16-bit unsigned int

// the setting the game itself is played at, so the positions are realistic
#define REF_DEPTH		3
#define REF_NODES		2000

/*-----------------------------------------------------------------------*/
int test_RunBudgetSurvey(int verbose)
{
	t_engUndo undo;
	char side = SIDE_WHITE, ply, d;
	unsigned long total[BUDGET_MAX_DEPTH + 1];
	unsigned int worst[BUDGET_MAX_DEPTH + 1];
	int samples[BUDGET_MAX_DEPTH + 1];

	for(d = 0; d <= BUDGET_MAX_DEPTH; ++d)
	{
		total[d] = 0;
		worst[d] = 0;
		samples[d] = 0;
	}

	eng_SetStartPosition();

	printf("nodes needed to complete each depth, through a real game\n");
	printf("  ply");
	for(d = 1; d <= BUDGET_MAX_DEPTH; ++d)
		printf("      d%d", d);
	printf("\n");

	for(ply = 0; ply < BUDGET_PLIES; ++ply)
	{
		t_searchResult result, play;

		printf("  %3d", ply + 1);

		for(d = 1; d <= BUDGET_MAX_DEPTH; ++d)
		{
			search_Best(side, d, BUDGET_CEILING, &result);

			// m_depth below d means the ceiling stopped it, not the depth
			if(result.m_depth < d)
				printf("    >60k");
			else
			{
				printf("  %6u", result.m_nodes);
				// the middlegame is what the budgets have to survive, so
				// summarise from ply 7 on rather than over the opening
				if(ply >= 6)
				{
					total[d] += result.m_nodes;
					++samples[d];
					if(result.m_nodes > worst[d])
						worst[d] = result.m_nodes;
				}
			}
		}
		printf("\n");

		search_Best(side, REF_DEPTH, REF_NODES, &play);
		if(!play.m_haveMove)
			break;
		eng_Make(&play.m_move, &undo);
		side = 1 - side;
	}

	// mean and worst matter differently: the mean sets the time a player feels,
	// the worst sets the budget needed to avoid throwing an iteration away
	printf("\n  middlegame, ply 7 on (n = completed samples out of %d):\n",
	       BUDGET_PLIES - 6);
	for(d = 1; d <= BUDGET_MAX_DEPTH; ++d)
		printf("    d%d  mean %6lu   worst %6u   completed %d\n", d,
		       samples[d] ? total[d] / samples[d] : 0, worst[d], samples[d]);

	(void)verbose;
	return 0;
}
