/*
 *	engineperft.c
 *	cc65 Chess - test support
 *
 *	Perft against the new 0x88 core in src/engine.c.  Unlike the old perft,
 *	which had to drive board_ProcessAction and the undo stack because there was
 *	no unmake, this one is a plain generate / make / recurse / unmake loop.
 *
 *	These numbers are expected to match the published references exactly, at
 *	every depth, with no divergence - including the under-promotions the old
 *	generator never produced.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "testutil.h"

#define MAX_PERFT_DEPTH		6
#define MAX_PERFT_PLIES		8

typedef struct tag_EnginePerftPos
{
	const char	*m_name;
	const char	*m_fen;
	long		 m_expected[MAX_PERFT_DEPTH];	// 0 = not published here
} t_EnginePerftPos;

// Verified against chessprogramming.org/Perft_Results
static const t_EnginePerftPos stc_positions[] =
{
	{ "initial",    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
	  { 20, 400, 8902, 197281, 4865609, 119060324 } },
	{ "kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
	  { 48, 2039, 97862, 4085603, 193690690, 0 } },
	{ "endgame",    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
	  { 14, 191, 2812, 43238, 674624, 11030083 } },
	{ "promotion",  "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
	  { 6, 264, 9467, 422333, 15833292, 0 } },
	{ "middlegame", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
	  { 44, 1486, 62379, 2103487, 89941194, 0 } },
};

#define NUM_ENGINE_POSITIONS ((int)(sizeof(stc_positions)/sizeof(stc_positions[0])))

// One move list per ply, the way the search will do it
static t_engMove st_arena[MAX_PERFT_PLIES][ENG_MAX_MOVES];

/*-----------------------------------------------------------------------*/
char test_EngineSetFEN(const char *fen)
{
	const char *p = fen;
	char row = 0, file = 0, side = SIDE_WHITE;

	eng_Clear();

	for(; *p && *p != ' '; ++p)
	{
		if(*p == '/')
		{
			++row;
			file = 0;
			continue;
		}
		if(*p >= '1' && *p <= '8')
		{
			file += (*p - '0');
			continue;
		}

		{
			char white = (*p >= 'A' && *p <= 'Z');
			char lower = white ? (*p - 'A' + 'a') : *p;
			char piece = NONE;
			char sq = (row << 4) | file;

			switch(lower)
			{
				case 'p': piece = PAWN;   break;
				case 'n': piece = KNIGHT; break;
				case 'b': piece = BISHOP; break;
				case 'r': piece = ROOK;   break;
				case 'q': piece = QUEEN;  break;
				case 'k': piece = KING;   break;
			}

			if(NONE != piece && !ENG_OFFBOARD(sq))
			{
				geBoard[sq] = piece | (white ? PIECE_WHITE : 0);
				if(KING == piece)
					geKing[white ? SIDE_WHITE : SIDE_BLACK] = sq;
			}
			++file;
		}
	}

	while(*p == ' ') ++p;
	if(*p == 'b')
		side = SIDE_BLACK;
	while(*p && *p != ' ') ++p;

	while(*p == ' ') ++p;
	geCastle = 0;
	for(; *p && *p != ' '; ++p)
	{
		switch(*p)
		{
			case 'K': geCastle |= ENG_CASTLE_WK; break;
			case 'Q': geCastle |= ENG_CASTLE_WQ; break;
			case 'k': geCastle |= ENG_CASTLE_BK; break;
			case 'q': geCastle |= ENG_CASTLE_BQ; break;
		}
	}

	while(*p == ' ') ++p;
	geEP = ENG_NO_SQUARE;
	if(*p && *p != '-')
		geEP = ENG_FROM_TILE(test_Square(p));

	// the pieces went straight onto the board, so the running evaluation has
	// to be rebuilt from them
	eval_Refresh();

	return side;
}

/*-----------------------------------------------------------------------*/
static long enginePerft(char side, int depth, int ply)
{
	t_engMove *moves = st_arena[ply];
	t_engUndo undo;
	char count, i;
	long nodes = 0;

	if(depth <= 0)
		return 1;

	count = eng_GenMoves(side, moves, ENG_MAX_MOVES);

	for(i = 0; i < count; ++i)
	{
		eng_Make(&moves[i], &undo);

		if(!eng_IsAttacked(geKing[side], 1 - side))
			nodes += (depth == 1) ? 1 : enginePerft(1 - side, depth - 1, ply + 1);

		eng_Unmake(&moves[i], &undo);
	}

	return nodes;
}

/*-----------------------------------------------------------------------*/
// Per-move breakdown, which is how a perft mismatch actually gets diagnosed:
// compare against a known-good engine one move at a time and recurse into
// whichever move disagrees
void test_EnginePerftDivide(const char *fen, int depth)
{
	t_engMove moves[ENG_MAX_MOVES];
	t_engUndo undo;
	char side = test_EngineSetFEN(fen);
	char count = eng_GenMoves(side, moves, ENG_MAX_MOVES), i;
	long total = 0;

	printf("divide, depth %d\n", depth);
	for(i = 0; i < count; ++i)
	{
		eng_Make(&moves[i], &undo);
		if(!eng_IsAttacked(geKing[side], 1 - side))
		{
			long n = (depth <= 1) ? 1 : enginePerft(1 - side, depth - 1, 0);
			char from[3], to[3];
			char promo = moves[i].m_flags & ENG_MF_PROMO;

			test_TileName(ENG_TO_TILE(moves[i].m_from), from);
			test_TileName(ENG_TO_TILE(moves[i].m_to), to);
			printf("  %s%s%c %ld\n", from, to,
			       promo ? ".rnbqkp"[promo] : ' ', n);
			total += n;
		}
		eng_Unmake(&moves[i], &undo);
	}
	printf("  total %ld\n", total);
}

/*-----------------------------------------------------------------------*/
int test_RunEnginePerft(int maxDepth, int verbose)
{
	int p, d, failures = 0;

	if(maxDepth > MAX_PERFT_DEPTH)
		maxDepth = MAX_PERFT_DEPTH;

	printf("engine perft (0x88 core, depth 1..%d)\n", maxDepth);
	printf("  %-11s %5s %12s %12s %8s  %s\n",
	       "position", "depth", "expected", "actual", "time", "status");

	for(p = 0; p < NUM_ENGINE_POSITIONS; ++p)
	{
		const t_EnginePerftPos *pos = &stc_positions[p];

		for(d = 1; d <= maxDepth; ++d)
		{
			long expected = pos->m_expected[d-1];
			long actual;
			char side;
			clock_t elapsed;

			if(!expected)
				continue;

			side = test_EngineSetFEN(pos->m_fen);

			elapsed = clock();
			actual = enginePerft(side, d, 0);
			elapsed = clock() - elapsed;

			if(actual != expected)
				++failures;

			printf("  %-11s %5d %12ld %12ld %7.2fs  %s",
			       pos->m_name, d, expected, actual,
			       (double)elapsed / CLOCKS_PER_SEC,
			       actual == expected ? "ok" : "FAIL");
			if(actual != expected)
				printf("  (%+ld)", actual - expected);
			printf("\n");

			if(verbose && actual != expected)
				printf("               run: chesstest divide \"%s\" %d\n", pos->m_fen, d);
		}
	}

	printf("  -> %d failing\n", failures);
	return failures;
}
