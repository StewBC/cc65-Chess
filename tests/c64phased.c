/*
 *	c64phased.c
 *	cc65 Chess - Phase D budget timing
 *
 *	Times the four skill depths at the old and new Harder/Very Hard node
 *	counts, on three fixed positions: opening, a scripted middlegame, and a
 *	KRK ending.  Same positions in every search, so old vs new is wall time
 *	of granted nodes, not two self-chosen games.
 *
 *	Build:
 *	  cl65 -t c64 -Oris -I../src -o c64phased.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64phased.c
 */

#include <stdio.h>
#include <time.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"

static const char sc_game[] =
{
	0x71,0x52, 0x01,0x22, 0x76,0x55, 0x06,0x25, 0x63,0x43, 0x13,0x33,
	0x64,0x54, 0x02,0x24, 0x75,0x53, 0x03,0x23, 0x74,0x76, 0x04,0x02,
	0x72,0x63, 0x02,0x01, 0x73,0x64, 0x03,0x04,
};
#define MIDDLEGAME_PLIES	16

typedef struct tag_case
{
	char depth;
	unsigned int nodes;
	char tag[4];
} t_case;

static const t_case sc_cases[6] =
{
	{ 3,   400, "L1" },
	{ 4,  1200, "L2" },
	{ 5, 15000, "L3o" },
	{ 5, 18000, "L3n" },
	{ 6, 60000u, "L4o" },
	{ 6, 65000u, "L4n" },
};

static t_engMove st_moves[80];

static void playScript(void)
{
	t_engUndo undo;
	char side = SIDE_WHITE, ply;

	eng_SetStartPosition();
	for(ply = 0; ply < MIDDLEGAME_PLIES; ++ply)
	{
		char count, i, from, to;

		from = sc_game[ply << 1];
		to = sc_game[(ply << 1) + 1];
		count = eng_GenMoves(side, st_moves, 80);
		for(i = 0; i < count; ++i)
			if(st_moves[i].m_from == from && st_moves[i].m_to == to)
			{
				eng_Make(&st_moves[i], &undo);
				break;
			}
		side = 1 - side;
	}
}

static void setEndgame(void)
{
	eng_Clear();
	geBoard[0x04] = KING;
	geBoard[0x74] = KING | PIECE_WHITE;
	geBoard[0x70] = ROOK | PIECE_WHITE;
	geKing[SIDE_BLACK] = 0x04;
	geKing[SIDE_WHITE] = 0x74;
	eval_Refresh();
	eng_HashReset();
}

static void timePos(char side, const char *name)
{
	char i;

	for(i = 0; i < 6; ++i)
	{
		t_searchResult result;
		clock_t start, taken;

		start = clock();
		search_Best(side, sc_cases[i].depth, sc_cases[i].nodes, &result);
		taken = clock() - start;
		printf("%s %s n=%u t=%lu d=%u\n",
		       name, sc_cases[i].tag, result.m_nodes,
		       (unsigned long)taken, (unsigned)result.m_depth);
	}
}

int main(void)
{
	printf("phase d budgets\n");

	eng_SetStartPosition();
	timePos(SIDE_WHITE, "O");

	playScript();
	timePos(SIDE_WHITE, "M");

	setEndgame();
	timePos(SIDE_WHITE, "E");

	printf("done.\n");
	for(;;)
		;
}
