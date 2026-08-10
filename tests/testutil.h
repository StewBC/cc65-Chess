/*
 *	testutil.h
 *	cc65 Chess - test support
 *
 *	Shared helpers for the native test build.  Nothing in tests/ is ever
 *	compiled into a cc65 target - it uses stdio, longs and other things the
 *	8-bit builds cannot afford.
 */

#ifndef _TESTUTIL_H_
#define _TESTUTIL_H_

/*-----------------------------------------------------------------------*/
// Tile <-> algebraic, in the UI's 0..63 numbering where tile 0 is a8
char test_Square(const char *algebraic);
void test_TileName(char tile, char *out3);

/*-----------------------------------------------------------------------*/
// Print the engine board
void test_DumpBoard(const char *tag);

/*-----------------------------------------------------------------------*/
// Set the engine from a FEN; returns the side to move
char test_EngineSetFEN(const char *fen);
// The other direction, for writing an opening book or lifting a position out
// of a game to look at.  "out" needs 90 bytes
void test_EngineGetFEN(char side, char *out);
// Per-move node counts, for tracking down a perft mismatch
void test_EnginePerftDivide(const char *fen, int depth);

/*-----------------------------------------------------------------------*/
// The suites.  Each returns the number of failures
int test_RunEnginePerft(int maxDepth, int verbose);
int test_RunQuiescenceGen(int verbose);
int test_RunBudgetSurvey(int verbose);
int test_RunGameFuzz(int seed, int games, int verbose);
int test_RunCastle(int verbose);
int test_RunRepetition(int verbose);
int test_RunOpening(int verbose);
int test_RunSelfPlay(int games, int maxPlies, int verbose);
int test_RunSearchTactics(int verbose);
int test_RunSearchAlwaysMoves(int verbose);
int test_RunSearchMateInOne(int verbose);
int test_RunSearchConversion(int verbose);
int test_RunSearchBench(int verbose);
int test_RunMatchSanity(int verbose);
int test_RunMatchTerms(int verbose);
int test_RunMatchDepth(int verbose);
int test_RunMatchEqualTime(int verbose);
int test_RunMatchEndgame(int verbose);
int test_RunMatchLadder(int verbose);
int test_RunMatchRepetition(int verbose);
int test_RunMatchMateDrive(int verbose);

#endif //_TESTUTIL_H_
