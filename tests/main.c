/*
 *	main.c
 *	cc65 Chess - test support
 *
 *	One binary, a few subcommands.  "all" is the gate: it runs everything that
 *	has a pass/fail answer and exits non-zero if anything failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "testutil.h"

/*-----------------------------------------------------------------------*/
static void usage(const char *argv0)
{
	printf("usage: %s <command> [options]\n\n", argv0);
	printf("  all                       run the pass/fail suite (exit != 0 on failure)\n");
	printf("  eperft [depth]            perft on the new 0x88 core (default 4)\n");
	printf("  budget                    nodes needed to complete each depth, through a game\n");
	printf("  qgen                      capture generator against the filtered full one\n");
	printf("  divide <fen> <depth>      per-move node counts for the new core\n");
	printf("  tactics                   search finds the obvious moves\n");
	printf("  bench                     search speed on the host\n");
	printf("  match [sanity|terms|depth|repeat]  configuration A vs B over the opening set\n");
	printf("  fuzz [seed] [games]       random games through the game path, undo/redo checked\n");
	printf("  castle                    castling and en passant rules\n");
	printf("  repeat                    repetition detection and its history\n");
	printf("  selfplay [games] [plies]  AI against itself, with timings\n");
	printf("\noptions: -v for more detail\n");
}

/*-----------------------------------------------------------------------*/
int main(int argc, char **argv)
{
	int failures = 0, verbose = 0, i;
	const char *command;

	for(i = 1; i < argc; ++i)
		if(!strcmp(argv[i], "-v"))
			verbose = 1;

	if(argc < 2)
	{
		usage(argv[0]);
		return 2;
	}
	command = argv[1];

	if(!strcmp(command, "all"))
	{
		printf("== cc65 Chess test suite ==\n\n");
		failures += test_RunCastle(verbose);
		printf("\n");
		failures += test_RunRepetition(verbose);
		printf("\n");
		failures += test_RunGameFuzz(1, 150, verbose);
		failures += test_RunGameFuzz(5000, 150, verbose);
		printf("\n");
		failures += test_RunEnginePerft(5, verbose);
		printf("\n");
		failures += test_RunQuiescenceGen(verbose);
		printf("\n");
		failures += test_RunSearchTactics(verbose);
		printf("\n");
		failures += test_RunSearchAlwaysMoves(verbose);
		printf("\n");
		failures += test_RunMatchSanity(0);
		printf("\n");
		failures += test_RunSelfPlay(1, 120, 0);
		printf("\n== %s ==\n", failures ? "FAILED" : "all green");
		return failures ? 1 : 0;
	}

	if(!strcmp(command, "tactics"))
		return test_RunSearchTactics(1) ? 1 : 0;

	if(!strcmp(command, "alwaysmoves"))
		return test_RunSearchAlwaysMoves(1) ? 1 : 0;

	if(!strcmp(command, "match"))
	{
		const char *what = argc > 2 ? argv[2] : "sanity";
		if(!strcmp(what, "terms")) return test_RunMatchTerms(verbose);
		if(!strcmp(what, "depth")) return test_RunMatchDepth(verbose);
		if(!strcmp(what, "time")) return test_RunMatchEqualTime(verbose);
		if(!strcmp(what, "endgame")) return test_RunMatchEndgame(verbose);
		if(!strcmp(what, "ladder")) return test_RunMatchLadder(verbose);
		if(!strcmp(what, "repeat")) return test_RunMatchRepetition(verbose);
		return test_RunMatchSanity(verbose);
	}

	if(!strcmp(command, "bench"))
		return test_RunSearchBench(verbose) ? 1 : 0;

	if(!strcmp(command, "eperft"))
		return test_RunEnginePerft(argc > 2 && argv[2][0] != '-' ? atoi(argv[2]) : 4, verbose) ? 1 : 0;

	if(!strcmp(command, "budget"))
		return test_RunBudgetSurvey(1);

	if(!strcmp(command, "qgen"))
		return test_RunQuiescenceGen(1) ? 1 : 0;

	if(!strcmp(command, "divide"))
	{
		if(argc < 4) { usage(argv[0]); return 2; }
		test_EnginePerftDivide(argv[2], atoi(argv[3]));
		return 0;
	}

	if(!strcmp(command, "fuzz"))
		return test_RunGameFuzz(argc > 2 && argv[2][0] != '-' ? atoi(argv[2]) : 1,
		                    argc > 3 && argv[3][0] != '-' ? atoi(argv[3]) : 200,
		                    verbose) ? 1 : 0;

	if(!strcmp(command, "castle"))
		return test_RunCastle(verbose) ? 1 : 0;

	if(!strcmp(command, "repeat"))
		return test_RunRepetition(verbose) ? 1 : 0;

	if(!strcmp(command, "selfplay"))
		return test_RunSelfPlay(argc > 2 && argv[2][0] != '-' ? atoi(argv[2]) : 1,
		                        argc > 3 && argv[3][0] != '-' ? atoi(argv[3]) : 200,
		                        verbose) ? 1 : 0;

	usage(argv[0]);
	return 2;
}
