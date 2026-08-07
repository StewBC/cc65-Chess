/*
 *	c64skill.c
 *	cc65 Chess - test support
 *
 *	What each skill level actually costs per move on a stock C64, playing a
 *	real game rather than searching the opening position.  Reads gcSearchSkill
 *	directly, so it measures what ships.
 *
 *	Prints one summary line a level instead of one line a move: fewer numbers
 *	to read off a screenshot is fewer numbers to read wrong.  "full" is the
 *	count of moves that actually reached the level's advertised depth - a level
 *	that rarely reaches its own depth is mistuned, which is exactly how the
 *	previous table went wrong.
 *
 *	Build:
 *	  cl65 -t c64 -Oris -I../src -o c64skill.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64skill.c
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"

// Only levels 1 and 2 have a time target, so only they are measured - and they
// are measured 20 plies deep, because the first few moves are not the game.
// Sampling the opening is exactly how the previous numbers came out a quarter
// too cheap
static const char sc_plies[2] = { 20, 20 };

/*-----------------------------------------------------------------------*/
int main(void)
{
	char lev;

	printf("skill cost per move, real game\n");

	for(lev = 0; lev < 2; ++lev)
	{
		t_engUndo undo;
		char side = SIDE_WHITE, ply, full = 0;
		unsigned long sum = 0, worst = 0, best = 65535ul, nodes = 0;

		eng_SetStartPosition();

		for(ply = 0; ply < sc_plies[lev]; ++ply)
		{
			t_searchResult result;
			clock_t start;
			unsigned long taken;

			start = clock();
			search_Best(side, gcSearchSkill[lev].m_depth,
			            gcSearchSkill[lev].m_nodes, &result);
			taken = (unsigned long)(clock() - start);

			sum += taken;
			nodes += result.m_nodes;
			if(taken > worst) worst = taken;
			if(taken < best)  best = taken;
			if(result.m_depth >= gcSearchSkill[lev].m_depth) ++full;

			if(!result.m_haveMove)
				break;

			eng_Make(&result.m_move, &undo);
			side = 1 - side;
		}

		// jiffies are 1/60 s on NTSC, which is what vice-run.sh uses
		printf("L%u d=%u mean=%lu lo=%lu hi=%lu full=%u/%u\n",
		       (unsigned)(lev + 1), (unsigned)gcSearchSkill[lev].m_depth,
		       sum / ply, best, worst, (unsigned)full, (unsigned)ply);
		printf("   mean nodes %lu\n", nodes / ply);
	}

	printf("done.\n");

	for(;;)
		;
}
