/*
 *	quiescence.c
 *	cc65 Chess - test support
 *
 *	One property, checked everywhere: eng_GenCaptures must return exactly the
 *	moves eng_GenMoves would have returned that quiescence keeps - the captures,
 *	en passant, and every promotion - in exactly the same order.
 *
 *	The "same order" half matters as much as the "same moves" half, and it is
 *	the part that would be easy to lose.  search.c breaks ties in move ordering
 *	by list position, so a capture generator that emitted the right set in a
 *	different order would leave the engine playing different moves in tied
 *	positions - legal, plausible, and silently not the engine that was measured.
 *
 *	Checked by walking the real perft trees, so the positions under test are the
 *	ones built to catch castling, en passant and promotion mistakes rather than
 *	positions chosen by whoever wrote the generator.
 */

#include <stdio.h>
#include "types.h"
#include "engine.h"
#include "testutil.h"

#define QT_MAX_PLY	6

static t_engMove st_full[QT_MAX_PLY][ENG_MAX_MOVES];
static t_engMove st_caps[QT_MAX_PLY][ENG_MAX_MOVES];

static long sl_positions;
static long sl_capMoves;
static long sl_fullMoves;
static int  si_failures;
static int  si_verbose;

/*-----------------------------------------------------------------------*/
// The moves quiescence searches: anything that takes a piece, plus every
// promotion.  This is the filter search.c used to apply after generating
// everything, kept here as the reference the fast generator is judged against
static char keptByQuiescence(const t_engMove *move)
{
	return (NONE != (geBoard[move->m_to] & PIECE_DATA)) ||
	       (0 != (move->m_flags & (ENG_MF_ENPASSANT | ENG_MF_PROMO)));
}

/*-----------------------------------------------------------------------*/
static void report(const char *what, char side, const t_engMove *want,
                   const t_engMove *got, char index)
{
	char from[3], to[3];

	if(si_failures > 8)		// one screenful is enough to debug from
		return;

	printf("    %s at index %d (side %d)\n", what, index, side);
	if(want)
	{
		test_TileName(ENG_TO_TILE(want->m_from), from);
		test_TileName(ENG_TO_TILE(want->m_to), to);
		printf("      expected %s-%s flags %02x\n", from, to, want->m_flags & 0xff);
	}
	if(got)
	{
		test_TileName(ENG_TO_TILE(got->m_from), from);
		test_TileName(ENG_TO_TILE(got->m_to), to);
		printf("      got      %s-%s flags %02x\n", from, to, got->m_flags & 0xff);
	}
	test_DumpBoard("      position");
}

/*-----------------------------------------------------------------------*/
// Compare the capture generator against the filtered full generator for the
// position as it stands
static int checkPosition(char side, int ply)
{
	t_engMove *full = st_full[ply];
	t_engMove *caps = st_caps[ply];
	char nFull, nCaps, i, want = 0;

	nFull = eng_GenMoves(side, full, ENG_MAX_MOVES);
	nCaps = eng_GenCaptures(side, caps, ENG_MAX_MOVES);

	++sl_positions;
	sl_fullMoves += nFull;
	sl_capMoves += nCaps;

	// walk the full list; every move the filter keeps must be the next move
	// the capture generator produced
	for(i = 0; i < nFull; ++i)
	{
		if(!keptByQuiescence(&full[i]))
			continue;

		if(want >= nCaps)
		{
			++si_failures;
			report("capture generator stopped early", side, &full[i], 0, want);
			return 1;
		}
		if(caps[want].m_from != full[i].m_from ||
		   caps[want].m_to != full[i].m_to ||
		   caps[want].m_flags != full[i].m_flags)
		{
			++si_failures;
			report("capture generator disagrees", side, &full[i], &caps[want], want);
			return 1;
		}
		++want;
	}

	if(want != nCaps)
	{
		++si_failures;
		report("capture generator produced extra moves", side, 0, &caps[want], want);
		return 1;
	}

	return 0;
}

/*-----------------------------------------------------------------------*/
// Walk the tree the way perft does, checking every position on the way
static void walk(char side, int depth, int ply)
{
	t_engMove moves[ENG_MAX_MOVES];
	t_engUndo undo;
	char count, i;

	if(checkPosition(side, ply))
		return;

	if(0 == depth || ply + 1 >= QT_MAX_PLY)
		return;

	count = eng_GenMoves(side, moves, ENG_MAX_MOVES);

	for(i = 0; i < count; ++i)
	{
		eng_Make(&moves[i], &undo);
		if(!eng_IsAttacked(geKing[side], 1 - side))
			walk(1 - side, depth - 1, ply + 1);
		eng_Unmake(&moves[i], &undo);

		if(si_failures)
			return;
	}
}

/*-----------------------------------------------------------------------*/
int test_RunQuiescenceGen(int verbose)
{
	// the perft set: the standard positions, chosen because they are the ones
	// that catch castling, en passant and promotion mistakes
	static const char *sc_fens[] =
	{
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
		"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
		"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
		"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
		"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
	};
	int i;

	si_failures = 0;
	si_verbose = verbose;
	sl_positions = sl_capMoves = sl_fullMoves = 0;

	printf("capture generator == filtered full generator\n");

	for(i = 0; i < (int)(sizeof(sc_fens)/sizeof(sc_fens[0])); ++i)
	{
		char side = test_EngineSetFEN(sc_fens[i]);

		walk(side, 3, 0);
		if(si_failures)
			printf("    position %d failed\n", i);
	}

	if(verbose || !si_failures)
		printf("  %ld positions, %ld capture moves against %ld full moves (%ld%%)\n",
		       sl_positions, sl_capMoves, sl_fullMoves,
		       sl_fullMoves ? (sl_capMoves * 100 / sl_fullMoves) : 0);

	printf("  -> %d failing\n", si_failures);
	return si_failures;
}
