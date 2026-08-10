/*
 *	c64evasion.c
 *	cc65 Chess - test support
 *
 *	What a search node costs on a real C64 with and without check evasions in
 *	quiescence, which is the one number doc/strength.md 5.1.5 had to estimate.
 *
 *	The point of this file rather than c64search.c is that it replays a *fixed*
 *	game.  Two builds that pick their own moves diverge after the first
 *	disagreement and then measure different positions, which is not a
 *	measurement of anything - a mistake already made once on the host, where two
 *	matches with different games put the cost at 30% and identical work put it
 *	at 12%.  Every position here is identical in both builds; only the cost of
 *	visiting it differs.
 *
 *	It also runs into a real middlegame rather than sitting in the opening.
 *	Evasions cost nothing where nobody is in check, so the opening understates
 *	them - the same reason c64level1.c exists alongside c64search.c.
 *
 *	Build both halves from the shipping configuration:
 *	  cl65 -t c64 -Oris -I../src -o c64evasion-on.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64evasion.c
 *	  cl65 -t c64 -Oris -I../src -DSEARCH_CHECK_EVASION=0 -o c64evasion-off.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64evasion.c
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"

/*-----------------------------------------------------------------------*/
// A level 2 game, generated on the host, stored as 0x88 from/to pairs
static const char sc_game[] =
{
	0x71,0x52, 0x01,0x22, 0x76,0x55, 0x06,0x25, 0x63,0x43, 0x13,0x33, 
	0x64,0x54, 0x02,0x24, 0x75,0x53, 0x03,0x23, 0x74,0x76, 0x04,0x02, 
	0x72,0x63, 0x02,0x01, 0x73,0x64, 0x03,0x04, 0x70,0x73, 0x04,0x03, 
	0x75,0x74, 0x03,0x04, 0x60,0x50, 0x07,0x06, 0x67,0x57, 0x06,0x07, 
	0x50,0x40, 0x04,0x03, 0x53,0x31, 0x25,0x44, 0x52,0x44, 0x33,0x44, 
	0x31,0x22, 0x23,0x22, 0x55,0x36, 0x22,0x62, 0x36,0x24, 0x15,0x24, 
	0x64,0x31, 0x62,0x53, 0x63,0x52, 0x53,0x31, 
};
#define GAME_PLIES	40

/*-----------------------------------------------------------------------*/
int main(void)
{
	// static, not on the stack: cc65 gives a function 256 bytes of locals and a
	// move list does not fit - the trap AGENTS.md names, walked into once here
	static t_engMove moves[80];
	t_engUndo undo;
	char side = SIDE_WHITE, ply;
	unsigned long jiffies = 0;
	unsigned long nodes = 0;

	printf("evasions=%u\n", (unsigned)SEARCH_CHECK_EVASION);

	eng_SetStartPosition();

	for(ply = 0; ply < GAME_PLIES; ++ply)
	{
		t_searchResult result;

		clock_t start, taken;
		char count, i, from, to;

		// time a search from this position, then discard its choice
		start = clock();
		search_Best(side, gcSearchSkill[1].m_depth,
		            gcSearchSkill[1].m_nodes, &result);
		taken = clock() - start;

		jiffies += (unsigned long)taken;
		nodes += (unsigned long)result.m_nodes;

		// and play the scripted move instead, so both builds stay in step
		from = sc_game[ply << 1];
		to   = sc_game[(ply << 1) + 1];
		count = eng_GenMoves(side, moves, 80);
		for(i = 0; i < count; ++i)
			if(moves[i].m_from == from && moves[i].m_to == to)
			{
				eng_Make(&moves[i], &undo);
				break;
			}
		if(i == count)
		{
			printf("desync at ply %u\n", (unsigned)ply);
			break;
		}
		side = 1 - side;
	}

	// nodes per second, times ten, so the ratio survives integer division
	printf("n=%lu t=%lu nps10=%lu\n", nodes, jiffies,
	       jiffies ? (nodes * 600UL) / jiffies : 0UL);
	printf("done.\n");

	for(;;)
		;
}
