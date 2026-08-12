/*
 *	pawnstruct.c
 *	cc65 Chess - test support
 *
 *	Purpose-built checks for the doubled/isolated term.  The score is rebuilt
 *	from the board at eval time, so these name positions and expected scores
 *	rather than file-count deltas.
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
static int expectStruct(const char *name, int want)
{
	int got;

	// eval_Position rebuilds gePawnStruct
	eval_Position(SIDE_WHITE);
	got = gePawnStruct;
	if(got != want)
	{
		printf("  %-28s FAIL structure score %d want %d\n",
		       name, got, want);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static int checkLiveSwitch(void)
{
	int with, without;
	char saved = geEvalTerms;

	// black doubled+isolated on a: white-positive -(D + 2*I) = +40
	test_EngineSetFEN("4k3/p7/p7/8/8/8/PP6/4K3 w - - 0 1");
	with = eval_Position(SIDE_WHITE);

	geEvalTerms = (char)(saved & ~EVAL_PAWNSTRUCT);
	without = eval_Position(SIDE_WHITE);
	geEvalTerms = saved;

	if(with - without != 40)
	{
		printf("  live switch                FAIL delta %d want 40 (on %d off %d)\n",
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

	(void)verbose;
	printf("pawn structure (doubled / isolated)\n");

	test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	failures += expectStruct("start", 0);
	if(!failures)
		printf("  start                      ok\n");

	// white doubled on e with d and f neighbours so isolation does not fire
	test_EngineSetFEN("4k3/8/8/8/8/3PPP2/4P3/4K3 w - - 0 1");
	failures += expectStruct("white doubled e", EVAL_PAWN_DOUBLED);
	if(!failures)
		printf("  white doubled e            ok\n");

	test_EngineSetFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
	failures += expectStruct("white isolated a", EVAL_PAWN_ISOLATED);
	if(!failures)
		printf("  white isolated a           ok\n");

	// black doubled and isolated on h: white-positive
	test_EngineSetFEN("4k3/7p/7p/8/8/8/8/4K3 w - - 0 1");
	failures += expectStruct("black dh",
		-(EVAL_PAWN_DOUBLED + 2 * EVAL_PAWN_ISOLATED));
	if(!failures)
		printf("  black doubled+isolated h   ok\n");

	// make then re-eval: isolated a-pawn pushed
	test_EngineSetFEN("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
	move.m_from = 0x60;
	move.m_to = 0x40;
	move.m_flags = ENG_MF_DOUBLEPUSH;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectStruct("after a2a4", EVAL_PAWN_ISOLATED);
	eng_Unmake(&move, &undo);
	failures += expectStruct("undo a2a4", EVAL_PAWN_ISOLATED);
	if(!failures)
		printf("  make/unmake isolated push  ok\n");

	// promotion clears the e-file pawn
	test_EngineSetFEN("4k3/4P3/8/8/8/8/8/4K3 w - - 0 1");
	move.m_from = 0x14;
	move.m_to = 0x04;
	move.m_flags = QUEEN;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectStruct("after promo", 0);
	eng_Unmake(&move, &undo);
	failures += expectStruct("undo promo", EVAL_PAWN_ISOLATED);
	if(!failures)
		printf("  promotion clears the file  ok\n");

	// capture of a black pawn
	test_EngineSetFEN("4k3/8/8/1p6/8/8/8/1N2K3 w - - 0 1");
	move.m_from = 0x71;
	move.m_to = 0x31;
	move.m_flags = 0;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectStruct("after nxb5", 0);
	eng_Unmake(&move, &undo);
	failures += expectStruct("undo nxb5", -EVAL_PAWN_ISOLATED);
	if(!failures)
		printf("  capture of enemy pawn      ok\n");

	failures += checkLiveSwitch();

	printf("  %s\n", failures ? "FAILED" : "ok");
	return failures;
}
#endif
