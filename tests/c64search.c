/*
 *	c64search.c
 *	cc65 Chess - test support
 *
 *	Measures what a *search* node costs on a real C64, as opposed to the perft
 *	node measured by c64perft.c.  A search node carries the evaluation and the
 *	move ordering as well, so it is the number the per-skill node budgets have
 *	to be set from.
 *
 *	Build:
 *	  cl65 -t c64 -Oris -I../src -o c64search.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64search.c
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"

/*-----------------------------------------------------------------------*/
// The opening position.  This was once described here as the conservative
// case, on the theory that a middlegame has fewer pieces and searches faster.
// That is wrong for the part of the game that matters: after both sides
// develop, nothing has been traded and the lines are open, so generation costs
// more and quiescence has more captures to chase - measured, 45 nodes/sec here
// against 35 by move 4.  Budgets taken from this position are about a quarter
// optimistic.  Use tests/c64level1.c, which times a real game, for anything
// that has to hold at the board
static void setStart(void)
{
	eng_SetStartPosition();
}

/*-----------------------------------------------------------------------*/
int main(void)
{
	static const unsigned int budgets[3] = { 400, 1600, 6000 };
	char b;

	printf("cc65 chess search bench\n");
	printf("clocks/sec %u\n\n", (unsigned)CLOCKS_PER_SEC);

	for(b = 0; b < 3; ++b)
	{
		t_searchResult result;
		clock_t start, taken;

		setStart();

		start = clock();
		search_Best(SIDE_WHITE, 6, budgets[b], &result);
		taken = clock() - start;

		printf("b=%u d=%u n=%u t=%lu\n",
		       budgets[b], (unsigned)result.m_depth, result.m_nodes,
		       (unsigned long)taken);
	}

	printf("\ndone.\n");

	for(;;)
		;
}
