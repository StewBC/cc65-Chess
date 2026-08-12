/*
 *	dev.c
 *	cc65 Chess - test support
 *
 *	Purpose-built checks for E3: queen off home while any original bishop
 *	or knight is still on its starting square.  Doses were 0 / 16 / 48
 *	before this file existed; the suite compiles the small dose.
 */

#include <stdio.h>
#include "types.h"
#include "engine.h"
#include "eval.h"
#include "testutil.h"

#if !EVAL_DEV_ON
int test_RunDev(int verbose)
{
	(void)verbose;
	printf("queen-before-minors: compiled out (EVAL_DEV_ON=0)\n");
	return 0;
}
#else

/*-----------------------------------------------------------------------*/
static int expectDev(const char *name, int want)
{
	int got;

	eval_Position(SIDE_WHITE);
	got = geDevScore;
	if(got != want)
	{
		printf("  %-32s FAIL geDevScore %d want %d\n", name, got, want);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static int checkLiveSwitch(void)
{
	int with, without;
	char saved = geEvalTerms;

	test_EngineSetFEN("rnbqkbnr/pppppppp/8/7Q/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
	with = eval_Position(SIDE_WHITE);

	geEvalTerms = (char)(saved & ~EVAL_DEV);
	without = eval_Position(SIDE_WHITE);
	geEvalTerms = saved;

	if(with - without != -EVAL_DEV_DOSE)
	{
		printf("  live switch                    FAIL delta %d want %d (on %d off %d)\n",
		       with - without, -EVAL_DEV_DOSE, with, without);
		return 1;
	}
	printf("  live switch                    ok\n");
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunDev(int verbose)
{
	int failures = 0;
	t_engMove move;
	t_engUndo undo;

	(void)verbose;
	printf("queen-before-minors (dose %d)\n", EVAL_DEV_DOSE);

	geEvalTerms |= EVAL_DEV;

	test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	failures += expectDev("start", 0);

	test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
	failures += expectDev("after e4, queen home", 0);

	test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/7Q/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
	failures += expectDev("white Qh5, minors home", -EVAL_DEV_DOSE);

	test_EngineSetFEN(
		"rnb1kbnr/pppppppp/8/8/7q/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	failures += expectDev("black Qh4, minors home", EVAL_DEV_DOSE);

	test_EngineSetFEN(
		"rnb1kbnr/pppppppp/8/7Q/7q/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
	failures += expectDev("both queens out", 0);

	test_EngineSetFEN("4k3/8/8/7Q/8/8/8/4K3 w - - 0 1");
	failures += expectDev("queen out, no minors", 0);

	test_EngineSetFEN("4k3/8/8/7Q/8/8/8/1N2K3 w - - 0 1");
	failures += expectDev("queen out, Nb1", -EVAL_DEV_DOSE);

	test_EngineSetFEN("4k3/8/8/7Q/8/8/8/2B1K3 w - - 0 1");
	failures += expectDev("queen out, Bc1", -EVAL_DEV_DOSE);

	test_EngineSetFEN(
		"r1bqkbnr/pppppppp/8/7Q/8/2N5/PPPPPPPP/R1B1KB1R w KQkq - 0 1");
	failures += expectDev("Qh5, both knights off, bishops home",
	                      -EVAL_DEV_DOSE);

	test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	move.m_from = 0x73;
	move.m_to = 0x37;
	move.m_flags = 0;
	move.m_score = 0;
	eng_Make(&move, &undo);
	failures += expectDev("make Qd1h5", -EVAL_DEV_DOSE);
	eng_Unmake(&move, &undo);
	failures += expectDev("unmake Qd1h5", 0);

	test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	move.m_from = 0x76;
	move.m_to = 0x55;
	move.m_flags = 0;
	eng_Make(&move, &undo);
	failures += expectDev("make Ng1f3, queen home", 0);

	if(!failures)
		printf("  positions                     ok\n");

	failures += checkLiveSwitch();

	printf("  %s\n", failures ? "FAILED" : "ok");
	return failures;
}
#endif
