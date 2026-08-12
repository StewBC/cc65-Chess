/*
 *	pawnstruct.c
 *	cc65 Chess - test support
 *
 *	Purpose-built checks for the incremental doubled/isolated term: the file
 *	counts and the aggregate score after FEN load, after a move, and after
 *	undo.  The fuzzer covers the long path; these name the positions.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "board.h"
#include "undo.h"
#include "search.h"
#include "testutil.h"

#if !EVAL_PAWNSTRUCT_ON
int test_RunPawnStruct(int verbose)
{
	(void)verbose;
	printf("pawn structure: compiled out (EVAL_PAWNSTRUCT_ON=0)\n");
	return 0;
}
#else

/*-----------------------------------------------------------------------*/
static int expectFiles(const char *name, char side, const char *want8)
{
	char f;

	for(f = 0; f < 8; ++f)
	{
		char got = gePawnFiles[side][f];
		char want = (char)(want8[f] - '0');

		if(got != want)
		{
			printf("  %-28s FAIL file %c side %d: got %d want %d\n",
			       name, 'a' + f, side, got, want);
			return 1;
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static int expectStruct(const char *name, int want)
{
	if(gePawnStruct != want)
	{
		printf("  %-28s FAIL structure score %d want %d\n",
		       name, gePawnStruct, want);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// Live switch: with the term cleared, eval_Position must ignore the files
// even though they are still maintained
static int checkLiveSwitch(void)
{
	int with, without;
	char saved = geEvalTerms;

	test_EngineSetFEN("4k3/pp6/8/8/8/8/PP6/4K3 w - - 0 1");
	// white a+b connected, black a+b connected: structure 0 either way
	// put white doubled on the a-file instead
	test_EngineSetFEN("4k3/p7/p7/8/8/8/PP6/4K3 w - - 0 1");
	// black: two on a (doubled + both isolated) = 1*DOUBLED + 2*ISOLATED
	// white: a and b, connected = 0
	// black defects are white-positive: -(1*D + 2*I) wait no:
	// black sign = -1, score += -1 * (1*DOUBLED + 2*ISOLATED)
	// DOUBLED=-10, ISOLATED=-20
	// score += -1 * (1*(-10) + 2*(-20)) = -1 * (-10-40) = 50
	with = eval_Position(SIDE_WHITE);

	geEvalTerms = (char)(saved & ~EVAL_PAWNSTRUCT);
	eval_Refresh();
	without = eval_Position(SIDE_WHITE);
	geEvalTerms = saved;
	eval_Refresh();

	if(with - without != 50)
	{
		printf("  live switch                FAIL delta %d want 50 (on %d off %d)\n",
		       with - without, with, without);
		return 1;
	}
	printf("  live switch                ok\n");
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunPawnStruct(int verbose)
{
	int failures = 0;
	t_engMove move;
	t_engUndo undo;
	char side;

	(void)verbose;
	printf("pawn structure (doubled / isolated)\n");

	// starting position: every file has one pawn a side, all have neighbours
	// on the edge files... a-file has only b as neighbour, and b has a pawn,
	// so nothing is isolated.  Score 0
	side = test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	(void)side;
	failures += expectFiles("start white", SIDE_WHITE, "11111111");
	failures += expectFiles("start black", SIDE_BLACK, "11111111");
	failures += expectStruct("start", 0);
	if(!failures)
		printf("  start                      ok\n");

	// white doubled on the e-file with d and f neighbours so isolation does
	// not also fire: files d=1, e=2, f=1
	test_EngineSetFEN("4k3/8/8/8/8/3PPP2/4P3/4K3 w - - 0 1");
	failures += expectFiles("white doubled e", SIDE_WHITE, "00012100");
	failures += expectStruct("white doubled e", EVAL_PAWN_DOUBLED);
	if(!failures)
		printf("  white doubled e            ok\n");

	// white isolated a-pawn only
	test_EngineSetFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
	failures += expectFiles("white isolated a", SIDE_WHITE, "10000000");
	failures += expectStruct("white isolated a", EVAL_PAWN_ISOLATED);
	if(!failures)
		printf("  white isolated a           ok\n");

	// black doubled and isolated on the h-file: one extra (doubled) and two
	// isolated pawns.  Black defects are white-positive
	test_EngineSetFEN("4k3/7p/7p/8/8/8/8/4K3 w - - 0 1");
	failures += expectFiles("black dh", SIDE_BLACK, "00000002");
	failures += expectStruct("black dh",
		-(EVAL_PAWN_DOUBLED + 2 * EVAL_PAWN_ISOLATED));
	if(!failures)
		printf("  black doubled+isolated h   ok\n");

	// make / unmake: push the white a-pawn so it stays isolated
	test_EngineSetFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
	move.m_from = 0x60;		// a2
	move.m_to = 0x40;		// a4
	move.m_flags = ENG_MF_DOUBLEPUSH;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectFiles("after a2a4", SIDE_WHITE, "10000000");
	failures += expectStruct("after a2a4", EVAL_PAWN_ISOLATED);
	eng_Unmake(&move, &undo);
	failures += expectFiles("undo a2a4", SIDE_WHITE, "10000000");
	failures += expectStruct("undo a2a4", EVAL_PAWN_ISOLATED);
	if(!failures)
		printf("  make/unmake isolated push  ok\n");

	// promotion: white pawn leaves the e-file
	test_EngineSetFEN("4k3/4P3/8/8/8/8/8/4K3 w - - 0 1");
	failures += expectFiles("before promo", SIDE_WHITE, "00001000");
	move.m_from = 0x14;		// e7
	move.m_to = 0x04;		// e8
	move.m_flags = QUEEN;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectFiles("after promo", SIDE_WHITE, "00000000");
	failures += expectStruct("after promo", 0);
	eng_Unmake(&move, &undo);
	failures += expectFiles("undo promo", SIDE_WHITE, "00001000");
	if(!failures)
		printf("  promotion clears the file  ok\n");

	// capture of a black pawn on the b-file by a white knight - no white pawn
	// moves, black loses the file
	test_EngineSetFEN("4k3/8/8/1p6/8/8/8/1N2K3 w - - 0 1");
	failures += expectFiles("before nxb5", SIDE_BLACK, "01000000");
	move.m_from = 0x71;		// b1
	move.m_to = 0x31;		// b5
	move.m_flags = 0;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectFiles("after nxb5", SIDE_BLACK, "00000000");
	failures += expectStruct("after nxb5", 0);
	eng_Unmake(&move, &undo);
	failures += expectFiles("undo nxb5", SIDE_BLACK, "01000000");
	if(!failures)
		printf("  capture of enemy pawn      ok\n");

	failures += checkLiveSwitch();

	// refresh must rebuild after a FEN that never went through make
	{
		int running;

		test_EngineSetFEN("4k3/pp6/8/8/8/8/P7/4K3 w - - 0 1");
		// white isolated a; black a+b connected
		running = gePawnStruct;
		eval_Refresh();
		if(running != gePawnStruct || running != EVAL_PAWN_ISOLATED)
		{
			printf("  refresh agrees            FAIL running %d full %d\n",
			       running, gePawnStruct);
			++failures;
		}
		else
			printf("  refresh agrees            ok\n");
	}

	printf("  %s\n", failures ? "FAILED" : "ok");
	return failures;
}
#endif
