/*
 *	c64level1.c
 *	cc65 Chess - test support
 *
 *	c64search.c times the *opening* position, on the stated grounds that it is
 *	the conservative case.  That assumption is worth checking: after both sides
 *	develop, the piece count has not dropped but the lines have opened, so the
 *	generator has more to do and quiescence has more captures to chase.  If the
 *	early middlegame is slower than the opening, every budget derived from the
 *	opening is optimistic and the skill-level times are understated.
 *
 *	So this plays a real game at the level 1 setting - depth 2, 500 nodes, both
 *	sides - and prints the time for each move as it goes.  That is exactly what
 *	someone sitting in front of the game experiences.
 *
 *	Build:
 *	  cl65 -t c64 -Oris -I../src -o c64level1.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64level1.c
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"

#define PLIES	18

/*-----------------------------------------------------------------------*/
int main(void)
{
	t_engUndo undo;
	char side = SIDE_WHITE, ply;

	printf("level 1: d=%u n=%u\n",
	       (unsigned)gcSearchSkill[0].m_depth, gcSearchSkill[0].m_nodes);

	eng_SetStartPosition();

	for(ply = 0; ply < PLIES; ++ply)
	{
		t_searchResult result;
		clock_t start, taken;

		start = clock();
		search_Best(side, gcSearchSkill[0].m_depth, gcSearchSkill[0].m_nodes, &result);
		taken = clock() - start;

		// move number as a player counts it, then jiffies and nodes actually spent
		printf("%u%c t=%lu n=%u\n",
		       (unsigned)((ply >> 1) + 1), (side == SIDE_WHITE) ? 'w' : 'b',
		       (unsigned long)taken, result.m_nodes);

		if(!result.m_haveMove)
			break;

		eng_Make(&result.m_move, &undo);
		side = 1 - side;
	}

	printf("done.\n");

	for(;;)
		;
}
