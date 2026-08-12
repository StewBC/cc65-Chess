/*
 *	legality.c
 *	cc65 Chess - test support
 *
 *	C2 gate: eng_LeavesInCheck must agree with eng_IsAttacked after every make,
 *	including the cases that made the pin-set idea dangerous - en passant
 *	discovery, every slider direction, aligned but free pieces, capturing the
 *	would-be attacker, double check and interpositions while already checked.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "testutil.h"

static int si_failures;

/*-----------------------------------------------------------------------*/
static void check(const char *what, int got, int want)
{
	if(got != want)
	{
		printf("    %-56s got %d, wanted %d\n", what, got, want);
		++si_failures;
	}
}

/*-----------------------------------------------------------------------*/
// Make the named pseudo-legal move and compare the two legality oracles.
static void expectLeave(const char *fen, const char *from, const char *to,
                        int wantIllegal, const char *label)
{
	t_engMove moves[ENG_MAX_MOVES];
	t_engUndo undo;
	char side = test_EngineSetFEN(fen);
	char count = eng_GenMoves(side, moves, ENG_MAX_MOVES);
	char i, wasInCheck, found = 0;
	char f = ENG_FROM_TILE(test_Square(from));
	char t = ENG_FROM_TILE(test_Square(to));

	wasInCheck = eng_InCheck(side);
	for(i = 0; i < count; ++i)
	{
		if(moves[i].m_from != f || moves[i].m_to != t)
			continue;
		found = 1;
		eng_Make(&moves[i], &undo);
		{
			char full = eng_IsAttacked(geKing[side], 1 - side);
			char fast = eng_LeavesInCheck(side, &moves[i], wasInCheck);

			check(label, fast, wantIllegal);
			if(full != wantIllegal)
			{
				printf("    %s: full oracle disagrees with expected (%d vs %d)\n",
				       label, full, wantIllegal);
				++si_failures;
			}
			if(full != fast)
			{
				printf("    %s: fast != full (%d vs %d)\n", label, fast, full);
				++si_failures;
			}
		}
		eng_Unmake(&moves[i], &undo);
		break;
	}
	if(!found)
	{
		printf("    %s: move %s-%s not generated\n", label, from, to);
		++si_failures;
	}
}

/*-----------------------------------------------------------------------*/
// Every generated move in a position: fast and full must match.
static void agreeAll(const char *fen, const char *label)
{
	t_engMove moves[ENG_MAX_MOVES];
	t_engUndo undo;
	char side = test_EngineSetFEN(fen);
	char count = eng_GenMoves(side, moves, ENG_MAX_MOVES);
	char i, wasInCheck = eng_InCheck(side);

	for(i = 0; i < count; ++i)
	{
		char full, fast;

		eng_Make(&moves[i], &undo);
		full = eng_IsAttacked(geKing[side], 1 - side) ? 1 : 0;
		fast = eng_LeavesInCheck(side, &moves[i], wasInCheck) ? 1 : 0;
		eng_Unmake(&moves[i], &undo);
		if(full != fast)
		{
			char fname[3], tname[3];

			test_TileName(ENG_TO_TILE(moves[i].m_from), fname);
			test_TileName(ENG_TO_TILE(moves[i].m_to), tname);
			printf("    %s: %s-%s flags %02x full %d fast %d\n",
			       label, fname, tname, moves[i].m_flags & 0xff, full, fast);
			++si_failures;
			if(si_failures > 12)
				return;
		}
	}
}

/*-----------------------------------------------------------------------*/
int test_RunLegality(int verbose)
{
	si_failures = 0;
	(void)verbose;

	printf("on-demand legality (fast == full)\n");

	// Horizontal en passant discovery: white king a5, white pawn d5, black
	// pawn c5 (ep c6), black rook h5.  d5xc6 ep clears c5 and d5 so the rook
	// checks the king along the fifth rank.
	expectLeave(
		"8/8/8/K1pP3r/8/8/8/4k3 w - c6 0 1",
		"d5", "c6", 1,
		"horizontal ep discovery leaves king in check");

	// Same shape with the king off the rook's rank - ep is legal.
	expectLeave(
		"8/8/8/2pP3r/8/8/8/K3k3 w - c6 0 1",
		"d5", "c6", 0,
		"ep capture with no discovery is legal");

	// Orthogonal pin: black rook e8, white queen e4, white king e1.
	expectLeave(
		"4r3/8/8/8/4Q3/8/8/4K3 w - - 0 1",
		"e4", "d5", 1,
		"moving off an orthogonal pin is illegal");
	expectLeave(
		"4r3/8/8/8/4Q3/8/8/4K3 w - - 0 1",
		"e4", "e5", 0,
		"sliding along an orthogonal pin is legal");

	// Diagonal pin: black bishop g7, white bishop d4, white king b2.
	expectLeave(
		"8/6b1/8/8/3B4/8/1K6/8 w - - 0 1",
		"d4", "c5", 1,
		"moving off a diagonal pin is illegal");
	expectLeave(
		"8/6b1/8/8/3B4/8/1K6/8 w - - 0 1",
		"d4", "e5", 0,
		"sliding along a diagonal pin is legal");

	// Aligned but not pinned: friendly pawn beyond the piece blocks the rook.
	expectLeave(
		"4r3/8/8/4P3/4Q3/8/8/4K3 w - - 0 1",
		"e4", "d5", 0,
		"aligned but blocked beyond is free to move");

	// Capturing the would-be attacker resolves the pin.
	expectLeave(
		"4r3/8/8/8/4Q3/8/8/4K3 w - - 0 1",
		"e4", "e8", 0,
		"capturing the pinning rook is legal");

	// Already in check: must not stand on a non-resolving move.
	// White king e1 checked by black rook e8; interpose with bishop.
	expectLeave(
		"4r3/8/8/8/8/8/8/4K1B1 w - - 0 1",
		"g1", "e3", 0,
		"interposing while in check is legal");
	expectLeave(
		"4r3/8/8/8/8/8/8/4K1B1 w - - 0 1",
		"g1", "f2", 1,
		"non-resolving move while in check is illegal");

	// Double check: only king moves work.  Black rook e8 and bishop c3 vs king e1.
	expectLeave(
		"4r3/8/8/8/8/2b5/8/4K3 w - - 0 1",
		"e1", "d1", 0,
		"king step out of double check is legal");
	expectLeave(
		"4r3/8/8/8/8/2b5/8/4K3 w - - 0 1",
		"e1", "e2", 1,
		"king stays on double-check file is illegal");

	// Full-list agreement on the perft positions and a few pin-heavy ones.
	agreeAll("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "start");
	agreeAll("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", "kiwipete");
	agreeAll("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", "endgame");
	agreeAll("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", "promotion");
	agreeAll("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", "middlegame");
	agreeAll("4r3/8/8/8/4N3/8/8/4K3 w - - 0 1", "ortho pin");
	agreeAll("8/8/8/K1pP3r/8/8/8/4k3 w - c6 0 1", "ep discovery pos");
	agreeAll("4r3/8/8/8/8/2b5/8/4K3 w - - 0 1", "double check");

	printf("  -> %d failing\n", si_failures);
	return si_failures;
}
