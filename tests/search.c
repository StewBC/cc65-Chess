/*
 *	search.c
 *	cc65 Chess - test support
 *
 *	Two things are checked here.  First that the search finds moves a chess
 *	player would call obvious - mates, free material, and the difference
 *	between mate and stalemate.  Then that it beats the old engine over the
 *	board, which is the only claim that really matters.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "eval.h"
#include "search.h"
#include "board.h"
#include "undo.h"
#include "testutil.h"

/*-----------------------------------------------------------------------*/
typedef struct tag_Tactic
{
	const char	*m_name;
	const char	*m_fen;
	char		 m_side;			// side to move, for the outcome cases
	char		 m_depth;
	const char	*m_want;			// expected move as "e2e4", or 0
	char		 m_wantOutcome;		// OUTCOME_* to expect, or 0 to skip
	char		 m_wantMate;		// non-zero if the score should be a mate
	int			 m_minScore;		// score must reach this, or 0 to skip
} t_Tactic;

static const t_Tactic stc_tactics[] =
{
	{ "back rank mate in 1", "6k1/5ppp/8/8/8/8/8/R3K2R w - - 0 1",
	  SIDE_WHITE, 2, "a1a8", 0, 1, 0 },

	{ "take the free queen", "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1",
	  SIDE_WHITE, 3, "d1d5", 0, 0, 0 },

	// More than one move reaches the promotion inside a 3 ply horizon, so
	// what is being asserted is that the search sees the new queen at all,
	// not which route it takes to her
	{ "see the promotion", "8/4P3/8/8/8/8/k7/7K w - - 0 1",
	  SIDE_WHITE, 3, 0, 0, 0, 800 },

	{ "recognise checkmate", "R5k1/5ppp/8/8/8/8/8/6K1 b - - 0 1",
	  SIDE_BLACK, 1, 0, OUTCOME_CHECKMATE, 0, 0 },

	{ "recognise stalemate", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
	  SIDE_BLACK, 1, 0, OUTCOME_STALEMATE, 0, 0 },
};

#define NUM_TACTICS ((int)(sizeof(stc_tactics)/sizeof(stc_tactics[0])))

/*-----------------------------------------------------------------------*/
static void moveName(const t_engMove *move, char *out)
{
	test_TileName(ENG_TO_TILE(move->m_from), out);
	test_TileName(ENG_TO_TILE(move->m_to), out + 2);
	out[4] = '\0';
}

/*-----------------------------------------------------------------------*/
int test_RunSearchTactics(int verbose)
{
	int t, failures = 0;

	printf("search tactics\n");

	for(t = 0; t < NUM_TACTICS; ++t)
	{
		const t_Tactic *tac = &stc_tactics[t];
		char side = test_EngineSetFEN(tac->m_fen);

		// A position where the side that just moved is still in check is not
		// reachable in a real game, and neither is one with the kings next to
		// each other.  Either means the test itself is wrong
		{
			signed char df = ENG_FILE(geKing[0]) - ENG_FILE(geKing[1]);
			signed char dr = ENG_ROW(geKing[0]) - ENG_ROW(geKing[1]);
			if(df < 0) df = -df;
			if(dr < 0) dr = -dr;
			if(df <= 1 && dr <= 1)
			{
				printf("  %-22s FAIL illegal test position: kings adjacent\n", tac->m_name);
				++failures;
				continue;
			}
		}

		if(eng_InCheck(1 - side))
		{
			printf("  %-22s FAIL illegal test position\n", tac->m_name);
			++failures;
			continue;
		}

		if(tac->m_wantOutcome)
		{
			char got = search_Outcome(tac->m_side);
			const char *names[] = { "invalid", "ok", "check", "checkmate", "draw", "stalemate" };

			if(got != tac->m_wantOutcome)
			{
				printf("  %-22s FAIL wanted %s got %s\n", tac->m_name,
				       names[tac->m_wantOutcome], names[got]);
				++failures;
			}
			else if(verbose)
				printf("  %-22s ok   %s\n", tac->m_name, names[got]);
			continue;
		}

		{
			t_searchResult result;
			char got[5];

			search_Best(side, tac->m_depth, 60000, &result);

			if(!result.m_haveMove)
			{
				printf("  %-22s FAIL no move returned\n", tac->m_name);
				++failures;
				continue;
			}

			moveName(&result.m_move, got);

			if(tac->m_want && strncmp(got, tac->m_want, 4))
			{
				printf("  %-22s FAIL wanted %s got %s (score %d, depth %d, %u nodes)\n",
				       tac->m_name, tac->m_want, got, result.m_score,
				       result.m_depth, result.m_nodes);
				++failures;
			}
			else if(tac->m_minScore && result.m_score < tac->m_minScore)
			{
				printf("  %-22s FAIL wanted score >= %d, got %d (%s)\n",
				       tac->m_name, tac->m_minScore, result.m_score, got);
				++failures;
			}
			else if(tac->m_wantMate && result.m_score < EVAL_MATE_IN(SEARCH_MAX_PLY))
			{
				printf("  %-22s FAIL wanted a mate score, got %d\n",
				       tac->m_name, result.m_score);
				++failures;
			}
			else if(verbose)
				printf("  %-22s ok   %s (score %d, depth %d, %u nodes)\n",
				       tac->m_name, got, result.m_score, result.m_depth, result.m_nodes);
		}
	}

	printf("  -> %d failing\n", failures);
	return failures;
}

/*-----------------------------------------------------------------------*/
// Mate in one, at the budget each skill level actually plays with.
//
// This exists because the tactics table above searches every position with
// 60000 nodes - the level 4 budget - so for the whole life of the project no
// test had ever asked the two weak levels whether they could see a mate.  They
// could not: level 1 found 27 of 60 mates in one, which a player at the board
// reported long before any test did.
//
// The cause was that quiescence stood pat and looked only at captures even when
// the side to move was in check, so a mating move looked quiet and mate in one
// was invisible to a depth 1 search - and 400 nodes rarely reaches depth 2.
//
// The positions are generated rather than invented, and that matters: the first
// attempt at this test used hand-written "mates" and two of them were not mates
// at all, which produced a page of failures that meant nothing.  These come out
// of random games, each has exactly one mating move, and each was verified by
// playing it and checking the opponent is in check with no legal reply
/*-----------------------------------------------------------------------*/
// C1 gate: the fused score+first-place path must try moves in exactly the
// same order as classic score-then-pickBest, not merely return the same root.
int test_RunSearchOrder(int verbose)
{
	static const char *sc_fens[] =
	{
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
		"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
		"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
		"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
		"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
		"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
		"2r3k1/pp3ppp/2n1b3/3qp3/8/2P1PN2/PP1Q1PPP/R3KB1R w KQ - 0 12",
	};
	int i, failures = 0;

	printf("move selection order (fused first == classic)\n");

	for(i = 0; i < (int)(sizeof(sc_fens)/sizeof(sc_fens[0])); ++i)
	{
		char side = test_EngineSetFEN(sc_fens[i]);

		if(!search_TestOrderSequence(side, 0))
		{
			++failures;
			printf("    full list order failed: %s\n", sc_fens[i]);
		}
		if(!search_TestOrderSequence(side, 1))
		{
			++failures;
			printf("    capture list order failed: %s\n", sc_fens[i]);
		}
		else if(verbose)
			printf("    ok %s\n", sc_fens[i]);
	}

	printf("  -> %d failing\n", failures);
	return failures;
}

/*-----------------------------------------------------------------------*/
// FollowPV off must be the shipping search.  FollowPV on must actually
// collect a line, or the switch is wired to nothing.
int test_RunSearchFollowPV(int verbose)
{
	t_searchResult off1, on, off2;
	char saved = geSearchFollowPV;
	char side;
	int failures = 0;

	printf("follow-pv live switch\n");
	side = test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

	geSearchFollowPV = 0;
	search_Best(side, 4, 1200, &off1);

	geSearchFollowPV = 1;
	search_Best(side, 4, 1200, &on);
	if(search_TestPVLength() < 2)
	{
		++failures;
		printf("  on: PV length %d, want at least 2\n",
		       (int)search_TestPVLength());
	}
	else if(verbose)
		printf("  on: PV length %d\n", (int)search_TestPVLength());

	geSearchFollowPV = 0;
	search_Best(side, 4, 1200, &off2);
	geSearchFollowPV = saved;

	if(off1.m_haveMove != off2.m_haveMove ||
	   off1.m_move.m_from != off2.m_move.m_from ||
	   off1.m_move.m_to != off2.m_move.m_to ||
	   off1.m_move.m_flags != off2.m_move.m_flags ||
	   off1.m_score != off2.m_score ||
	   off1.m_depth != off2.m_depth ||
	   off1.m_nodes != off2.m_nodes)
	{
		++failures;
		printf("  off after on differs from first off\n");
	}

	if(search_TestPVLength() != 0)
	{
		++failures;
		printf("  off left PV length %d\n", (int)search_TestPVLength());
	}

	printf("  -> %d failing\n", failures);
	return failures;
}

/*-----------------------------------------------------------------------*/
int test_RunSearchRootScores(int verbose)
{
	t_searchResult off1, on, off2;
	char saved = geSearchRootScores;
	char side;
	int failures = 0;

	printf("root-scores live switch\n");
	side = test_EngineSetFEN(
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

	geSearchRootScores = 0;
	search_Best(side, 4, 1200, &off1);

	geSearchRootScores = 1;
	search_Best(side, 4, 1200, &on);
	if(search_TestRootStored() < 2)
	{
		++failures;
		printf("  on: stored %d root scores, want at least 2\n",
		       (int)search_TestRootStored());
	}
	else if(verbose)
		printf("  on: stored %d root scores\n", (int)search_TestRootStored());

	geSearchRootScores = 0;
	search_Best(side, 4, 1200, &off2);
	geSearchRootScores = saved;

	if(off1.m_haveMove != off2.m_haveMove ||
	   off1.m_move.m_from != off2.m_move.m_from ||
	   off1.m_move.m_to != off2.m_move.m_to ||
	   off1.m_move.m_flags != off2.m_move.m_flags ||
	   off1.m_score != off2.m_score ||
	   off1.m_depth != off2.m_depth ||
	   off1.m_nodes != off2.m_nodes)
	{
		++failures;
		printf("  off after on differs from first off\n");
	}

	if(search_TestRootStored() != 0)
	{
		++failures;
		printf("  off left %d stored scores\n", (int)search_TestRootStored());
	}

	printf("  -> %d failing\n", failures);
	return failures;
}

/*-----------------------------------------------------------------------*/
int test_RunSearchMateInOne(int verbose)
{
	static const struct { const char *fen; const char *mate; } sc_mates[] =
	{
		{ "8/N3k1nr/3p4/3q2p1/P1P2r2/1P2K3/2b4P/B5R1 b - - 0 1", "d5d3" },
		{ "r1bqkb2/pp1pp1pr/2n5/2p2pNp/2QP2nP/2P1P3/PP2BPP1/RNB1K2R w KQq - 8 1", "c4f7" },
		{ "2r2bn1/R7/2p1p3/2pp1kp1/1n1P1P1r/Q3B1qp/PP4b1/RNK2B2 b - f3 0 1", "g3e1" },
		{ "rn2kbr1/2p1pp2/3p3p/p5p1/3P2Q1/4Bq2/1PP2P1n/RN2K1NR w q - 2 1", "g4c8" },
		{ "3k4/pr4br/3p1p2/4p2q/4P3/5PR1/n5K1/5R2 b - - 1 1", "h5h2" },
		{ "6k1/2p3nr/6Q1/2bpP3/1p3R1P/3Bn2K/8/3qB3 w - - 5 1", "g6h7" },
		{ "5bk1/nr2p3/1q1pPpp1/p2P1P2/1pp2Nn1/PPN2B2/RB1P3P/1Q3K1R b - - 1 1", "b6f2" },
		{ "5k1b/rb2p3/2np4/P1P2p2/N1B1Pn1Q/B1P2P1N/P6R/R3K3 w - - 1 1", "h4h8" },
		{ "6N1/1Nk5/2P3p1/8/2P1nrp1/6P1/7q/R1K2R2 b - - 0 1", "f4f1" },
		{ "1n3r2/4br2/1ppp3k/p2P2Rp/P4P1B/RP4Nb/2P5/3Q1B1K w - - 2 1", "d1h5" },
		{ "2kq3r/p2nb2p/2N2P2/2p3p1/2N5/1r1p3P/5PBR/B4K2 b - - 4 1", "b3b1" },
		{ "1rbqkbnr/pp1pp1p1/5pn1/2p5/2PP2p1/N2QPP1P/PP6/R1B1KBNR w KQk - 2 1", "d3g6" },
	};
	// Level 1 is not held to a clean sweep, and the reason is not slack.  Its
	// 400 nodes sometimes cannot complete even depth 1 in a sharp position -
	// tests/budget.c measures depth 1 at up to 3228 nodes there - and a search
	// that never finishes an iteration plays its first ordered move, which is a
	// capture.  That is the documented trade in gcSearchSkill, not a defect to
	// fix here.  The floor is set at what it actually achieves, so a change that
	// moves it in either direction gets noticed
	static const int sc_floor[SEARCH_NUM_SKILLS] = { 10, 12, 12, 12 };
	int count = (int)(sizeof(sc_mates)/sizeof(sc_mates[0]));
	int failures = 0, level, i;

	printf("mate in one, at each level's own budget\n");

	for(level = 0; level < SEARCH_NUM_SKILLS; ++level)
	{
		int missed = 0;

		for(i = 0; i < count; ++i)
		{
			t_searchResult result;
			char side = test_EngineSetFEN(sc_mates[i].fen);
			char got[5];

			search_Best(side, gcSearchSkill[level].m_depth,
			            gcSearchSkill[level].m_nodes, &result);

			if(!result.m_haveMove)
			{
				++missed;
				continue;
			}
			moveName(&result.m_move, got);
			if(strncmp(got, sc_mates[i].mate, 4))
			{
				++missed;
				if(verbose)
					printf("    level %d  %s: wanted %s got %s\n",
					       level + 1, sc_mates[i].fen, sc_mates[i].mate, got);
			}
		}

		// always printed: the count is the interesting number even when it
		// passes, because this is a figure that has moved before
		printf("  level %d (%5u nodes) %2d of %d%s\n",
		       level + 1, gcSearchSkill[level].m_nodes, count - missed, count,
		       (count - missed) < sc_floor[level] ? "   BELOW FLOOR" : "");

		if((count - missed) < sc_floor[level])
			++failures;
	}

	printf("  -> %d failing\n", failures);
	return failures;
}

/*-----------------------------------------------------------------------*/
// The AI must never fail to produce a move when it has one.
//
// This is a regression test for a bug that reached a real board: in a sharp
// middlegame the human moved, the engine returned no move at all, and the game
// handed the turn straight back - the AI appeared to give up.  The cause was a
// budget too small to finish even depth 1 (which in that position wanted 1404
// nodes, against the 152 a quiet position needs).  search_Best then returned
// m_haveMove = 0, and every caller reads that as "no legal moves", i.e.
// stalemate.
//
// The lesson worth keeping is about the *shape* of the failure rather than the
// arithmetic: running out of time is normal and must degrade to a worse move,
// never to no move.  So this checks the whole ladder of budgets, including ones
// far below anything the skill table would ever use, because the guarantee has
// to hold at any budget rather than at the ones currently shipping
int test_RunSearchAlwaysMoves(int verbose)
{
	// sharp positions - lots of captures, so depth 1 alone costs a great deal
	static const char *sc_fens[] =
	{
		// the position from the bug report, black to move
		"r2qkb1r/ppp2ppp/2n1bn2/3pp3/3PP3/2N1BN2/PPP2PPP/R2QKB1R b KQkq -",
		"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
		"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ -",
		"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -",
	};
	static const unsigned int sc_budgets[] = { 1, 5, 20, 100, 400, 2500, 15000 };
	int failures = 0, f, b, d;

	printf("search always returns a move when one exists\n");

	for(f = 0; f < (int)(sizeof(sc_fens)/sizeof(sc_fens[0])); ++f)
	{
		char side = test_EngineSetFEN(sc_fens[f]);

		for(b = 0; b < (int)(sizeof(sc_budgets)/sizeof(sc_budgets[0])); ++b)
		{
			for(d = 1; d <= 4; ++d)
			{
				t_searchResult result;

				side = test_EngineSetFEN(sc_fens[f]);
				search_Best(side, d, sc_budgets[b], &result);

				if(!result.m_haveMove)
				{
					printf("    FAIL position %d, depth %d, budget %u: no move\n",
					       f, d, sc_budgets[b]);
					++failures;
				}
				else if(verbose)
					printf("    position %d d=%d b=%-5u -> depth %d, %u nodes\n",
					       f, d, sc_budgets[b], result.m_depth, result.m_nodes);
			}
		}
	}

	// The other half of the guarantee: when there is genuinely no legal move,
	// search_Best must still say so.  searchRoot used to clear m_haveMove
	// whenever no root search *completed*, which was both too eager (it wiped
	// a banked move on a budget abort) and load-bearing for mate detection.
	// That reset is gone, so the two cases are checked together - "always
	// returns a move" is only correct if it does not also invent one
	{
		static const char *sc_mated[] =
		{
			"R5k1/5ppp/8/8/8/8/8/6K1 b - -",	// checkmate
			"7k/5Q2/6K1/8/8/8/8/8 b - -",		// stalemate
		};
		int m, dd;

		for(m = 0; m < 2; ++m)
		{
			for(dd = 1; dd <= 3; ++dd)
			{
				t_searchResult result;
				char side = test_EngineSetFEN(sc_mated[m]);

				search_Best(side, dd, 60000, &result);
				if(result.m_haveMove)
				{
					printf("    FAIL %s position, depth %d: invented a move\n",
					       m ? "stalemate" : "checkmate", dd);
					++failures;
				}
			}
		}
	}

	printf("  -> %d failing\n", failures);
	return failures;
}

/*-----------------------------------------------------------------------*/
// Nodes per second from the host, which is only useful next to the C64
// measurement in tests/c64perft.c - but it does show what the search costs
// per node relative to perft
int test_RunSearchBench(int verbose)
{
	static const char *positions[] =
	{
		"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
		"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
		"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
	};
	int p;

	(void)verbose;
	printf("search bench (host)\n");
	printf("  %-11s %6s %8s %8s %10s\n", "position", "depth", "nodes", "score", "nodes/sec");

	for(p = 0; p < 3; ++p)
	{
		t_searchResult result;
		char side = test_EngineSetFEN(positions[p]);
		clock_t taken;
		double secs;

		taken = clock();
		search_Best(side, 5, 60000, &result);
		taken = clock() - taken;
		secs = (double)taken / CLOCKS_PER_SEC;

		printf("  %-11d %6d %8u %8d %10.0f\n", p + 1, result.m_depth,
		       result.m_nodes, result.m_score, secs > 0 ? result.m_nodes / secs : 0);
	}

	return 0;
}

/*-----------------------------------------------------------------------*/
// Can it finish?
//
// A won ending is not won until it is mated, and for a long time this engine
// could not do it.  The Sargon II games are where it showed: playing White it
// reached a clean king and rook against a bare king on move 66, still had it
// on move 115, and drew by the fifty-move rule.  Twice, from two different
// openings, because it is the same game.
//
// The cause was that nothing in the evaluation preferred one bare-king
// position to another.  sc_pstKingEnd sends a king to the middle, and it is a
// per-piece table, so it said the same thing to the king being mated - the
// engine could see that its own king should come out and had no opinion at all
// about where the enemy king should be.  Every rook move scored alike and it
// wandered.  eval.c's mateDrive is the fix and this is the measurement.
//
// The move limit is the fifty-move rule itself rather than a number chosen
// here.  That is the constraint the engine actually failed, and it is stricter
// than it looks: a mate that takes 24 moves from a clean start is lost anyway
// when the ending is reached with half the counter already spent.  So the
// plies-to-mate column matters as much as the count, and it is printed.
//
// Both sides are the engine at the level under test, which makes the whole
// suite self-contained and every run identical.  It also means the defence is
// weak, so these numbers are the optimistic case - measured against Stockfish
// defending, level 2 converts 72 of 100 random won endings rather than the
// clean sweep below.  doc/measuring.md has that instrument
static const struct { const char *m_name; const char *m_fen; } stc_wins[] =
{
	// king and rook: the ending from the Sargon game first
	{ "KRK  a", "8/8/8/6R1/2K1k3/8/8/8 w - - 0 1" },
	{ "KRK  b", "4k3/8/8/8/8/8/6R1/4K3 w - - 0 1" },
	{ "KRK  c", "8/8/8/8/8/4k3/8/R3K3 w - - 0 1" },
	{ "KRK  d", "7k/8/8/8/8/8/R7/7K w - - 0 1" },
	{ "KRK  e", "8/2k5/8/8/8/8/5R2/4K3 w - - 0 1" },
	// king and queen, where the danger is stalemate rather than the counter
	{ "KQK  a", "8/8/8/4k3/8/8/8/3QK3 w - - 0 1" },
	{ "KQK  b", "3k4/8/8/8/8/8/5Q2/6K1 w - - 0 1" },
	{ "KQK  c", "8/8/4k3/8/8/2Q5/8/6K1 w - - 0 1" },
	{ "KQK  d", "8/5k2/8/8/8/8/1Q6/K7 w - - 0 1" },
	// two rooks, which needs no king at all and should never fail
	{ "KRRK a", "4k3/8/8/8/8/8/8/R3K2R w - - 0 1" },
	{ "KRRK b", "8/8/3k4/8/8/8/8/R5RK w - - 0 1" },
	// and with something left to defend with, which is what a real game hands
	// over rather than a bare king
	{ "KRvKP", "8/8/8/4k3/8/4p3/8/R3K3 w - - 0 1" },
	{ "KQvKP", "8/8/8/3k4/8/8/6p1/2Q1K3 w - - 0 1" },
};

/*-----------------------------------------------------------------------*/
// KBN vs bare king — the named E4 defect.  Level 4 can convert some positions
// when EVAL_KBN_ON; levels 1–3 cannot at their budgets.  Floors record that
#if EVAL_KBN_ON
static const struct { const char *m_name; const char *m_fen; } stc_kbn[] =
{
	{ "KBN a", "8/8/8/4k3/8/8/8/4KBN1 w - - 0 1" },
	{ "KBN b", "6k1/8/8/8/8/8/8/4KBN1 w - - 0 1" },
	{ "KBN c", "7k/5N2/8/8/8/8/8/4KB2 w - - 0 1" },
	{ "KBN d", "7k/6N1/6K1/8/8/8/8/5B2 w - - 0 1" },
	{ "KBN e", "k7/8/8/8/8/8/8/1NBK4 w - - 0 1" },
	{ "KBN f", "8/8/8/8/4k3/8/8/B3K1N1 w - - 0 1" },
	{ "KBN g", "4k3/8/8/8/8/8/8/4KBN1 w - - 0 1" },
	{ "KBN h", "8/8/3k4/8/8/8/8/2B1K1N1 w - - 0 1" },
};
#endif

int test_RunSearchConversion(int verbose)
{
	// What each level manages today.  Held to what it achieves rather than to
	// what it ought to, so that a change in either direction gets noticed -
	// the same rule as the mate-in-one floors above.  Level 1 has 400 nodes and
	// is not expected to sweep
	static const int sc_floor[SEARCH_NUM_SKILLS] = { 11, 13, 13, 13 };
	int count = (int)(sizeof(stc_wins)/sizeof(stc_wins[0]));
	int failures = 0, level, i;

	printf("won endings finished before the fifty-move rule\n");

	for(level = 0; level < SEARCH_NUM_SKILLS; ++level)
	{
		int mated = 0, plies = 0;

		for(i = 0; i < count; ++i)
		{
			char side = test_EngineSetFEN(stc_wins[i].m_fen);
			const char *why = "still going";
			int ply;

			undo_Init();

			// 200 plies is well past the fifty-move rule and is only here so a
			// capture that resets the counter cannot loop forever.  A queen
			// left en prise does exactly that, and finding out took a while
			for(ply = 0; ply < 200; ++ply)
			{
				t_searchResult result;
				char outcome = search_Outcome(side);

				if(OUTCOME_CHECKMATE == outcome)
				{
					++mated;
					plies += ply;
					why = 0;
					break;
				}
				// stalemate and the counter are both the full point lost, and
				// the queen endings can produce either
				if(OUTCOME_STALEMATE == outcome)      { why = "stalemate";  break; }
				if(geHalfmove >= 100)                 { why = "fifty-move"; break; }

				search_Best(side, gcSearchSkill[level].m_depth,
				            gcSearchSkill[level].m_nodes, &result);
				if(!result.m_haveMove)                { why = "no move";    break; }

				board_ApplyMove(&result.m_move, side);
				side = 1 - side;
			}

			if(verbose && why)
				printf("    level %d  %s: %s\n", level + 1, stc_wins[i].m_name, why);
		}

		printf("  level %d (%5u nodes) %2d of %d, mean %3d plies%s\n",
		       level + 1, gcSearchSkill[level].m_nodes, mated, count,
		       mated ? plies / mated : 0,
		       mated < sc_floor[level] ? "   BELOW FLOOR" : "");

		if(mated < sc_floor[level])
			++failures;
	}

#if EVAL_KBN_ON
	// Named defect: KBN vs K.  Floors are what the size-conscious form reaches
	// on the host with the suite's forced ON — not a claim that shipping mates
	// these (EVAL_KBN_ON defaults 0)
	{
		static const int sc_kbnFloor[SEARCH_NUM_SKILLS] = { 0, 0, 0, 1 };
		int kbnCount = (int)(sizeof(stc_kbn)/sizeof(stc_kbn[0]));

		printf("KBN vs bare king (E4)\n");
		geEvalTerms |= EVAL_KBN;

		for(level = 0; level < SEARCH_NUM_SKILLS; ++level)
		{
			int mated = 0, plies = 0;

			for(i = 0; i < kbnCount; ++i)
			{
				char side = test_EngineSetFEN(stc_kbn[i].m_fen);
				int ply, ok = 0;

				undo_Init();
				for(ply = 0; ply < 200; ++ply)
				{
					t_searchResult result;
					char outcome = search_Outcome(side);

					if(OUTCOME_CHECKMATE == outcome)
					{
						ok = 1;
						++mated;
						plies += ply;
						break;
					}
					if(OUTCOME_STALEMATE == outcome || geHalfmove >= 100)
						break;
					search_Best(side, gcSearchSkill[level].m_depth,
					            gcSearchSkill[level].m_nodes, &result);
					if(!result.m_haveMove)
						break;
					board_ApplyMove(&result.m_move, side);
					side = 1 - side;
				}
				if(verbose && !ok)
					printf("    level %d  %s: failed\n",
					       level + 1, stc_kbn[i].m_name);
			}

			printf("  level %d (%5u nodes) %2d of %d, mean %3d plies%s\n",
			       level + 1, gcSearchSkill[level].m_nodes, mated, kbnCount,
			       mated ? plies / mated : 0,
			       mated < sc_kbnFloor[level] ? "   BELOW FLOOR" : "");
			if(mated < sc_kbnFloor[level])
				++failures;
		}
		geEvalTerms = EVAL_ALL;
	}
#endif

	printf("  -> %d failing\n", failures);
	return failures;
}
