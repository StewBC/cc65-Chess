/*
 *	c64perft.c
 *	cc65 Chess - test support
 *
 *	The Phase 1 review gate.  Runs perft on the new 0x88 core on a real C64 and
 *	reports the time in jiffies, which is emulated time and so is unaffected by
 *	running the emulator in warp mode.
 *
 *	Perft is movegen + make + unmake + one attack test, which is the bulk of
 *	what a search node costs.  So nodes/sec here is the number the per-skill
 *	node budgets get derived from.
 *
 *	Build (not part of the game Makefile):
 *	  cl65 -t c64 -Oris -I../src -o c64perft.prg ../src/engine.c c64perft.c
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "engine.h"

// depth 3 needs 3 plies of move list; one spare
#define PLIES	4

static t_engMove st_arena[PLIES][ENG_MAX_MOVES];

/*-----------------------------------------------------------------------*/
static unsigned long perft(char side, char depth, char ply)
{
	t_engMove *moves = st_arena[ply];
	t_engUndo undo;
	char count, i;
	unsigned long nodes = 0;

	if(!depth)
		return 1;

	count = eng_GenMoves(side, moves);

	for(i = 0; i < count; ++i)
	{
		eng_Make(&moves[i], &undo);

		if(!eng_IsAttacked(geKing[side], 1 - side))
			nodes += (depth == 1) ? 1 : perft(1 - side, depth - 1, ply + 1);

		eng_Unmake(&moves[i], &undo);
	}

	return nodes;
}

/*-----------------------------------------------------------------------*/
int main(void)
{
	char depth;

	printf("cc65 chess 0x88 perft\n");
	printf("clocks/sec %u\n\n", (unsigned)CLOCKS_PER_SEC);

	for(depth = 1; depth <= 3; ++depth)
	{
		clock_t start, taken;
		unsigned long nodes;

		eng_SetStartPosition();

		start = clock();
		nodes = perft(SIDE_WHITE, depth, 0);
		taken = clock() - start;

		printf("d%u n=%lu t=%lu\n", (unsigned)depth, nodes, (unsigned long)taken);
	}

	printf("\ndone.\n");

	// hold the screen so the exit screenshot has something on it
	for(;;)
		;
}
