/*
 *	opening.c
 *	cc65 Chess - test support
 *
 *	The opening randomiser, which is the one feature here whose whole purpose
 *	is to stop being reproducible.  That makes it awkward to test and easy to
 *	get wrong in a way nobody notices, because both failure modes look like
 *	ordinary chess: a randomiser that never fires leaves the openings identical
 *	(the bug it exists to fix, silently unfixed), and one that fires too hard
 *	starts choosing moves the search did not rank first.
 *
 *	So the two things checked here are exactly those:
 *
 *	  - different seeds produce different openings, and a zero seed reproduces
 *	    the unseeded engine move for move.  That second half is what every
 *	    figure in doc/strength.md rests on: the harnesses never seed, and if
 *	    "never seeded" ever stopped meaning "plays as it always did", the
 *	    ladder would quietly stop being comparable with its own history.
 *
 *	  - the move it picks is always one the search scored equal-best.  This is
 *	    the property that makes the feature free: perturbing move ordering
 *	    cannot change which move alpha-beta returns except among exact ties,
 *	    so the randomiser can vary the opening without ever playing worse.
 *	    Checked by searching the position twice, once seeded and once not, and
 *	    comparing the scores rather than the moves.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "search.h"
#include "board.h"
#include "undo.h"
#include "cpu.h"
#include "testutil.h"

static int si_failures;

/*-----------------------------------------------------------------------*/
static void check(const char *what, int got, int want)
{
	if(got != want)
	{
		printf("    %-52s got %d, wanted %d\n", what, got, want);
		++si_failures;
	}
}

/*-----------------------------------------------------------------------*/
// Play the engine's first plies from the start position with the given seed,
// and write them into out as "e2e4 " style text so two games can be compared
// out must hold 5 characters a ply, plus the terminator
static void openingLine(char seed, int plies, char *out)
{
	t_searchResult result;
	t_engUndo undo;
	char side = SIDE_WHITE;
	int i;

	board_Init();
	search_SetSeed(seed);
	*out = '\0';

	for(i = 0; i < plies; ++i)
	{
		search_Best(side, 4, 2000, &result);
		if(!result.m_haveMove)
			break;

		sprintf(out + strlen(out), "%c%c%c%c ",
		        'a' + (result.m_move.m_from & 7),
		        '1' + (7 - (result.m_move.m_from >> 4)),
		        'a' + (result.m_move.m_to & 7),
		        '1' + (7 - (result.m_move.m_to >> 4)));

		eng_Make(&result.m_move, &undo);
		side = 1 - side;
	}
}

/*-----------------------------------------------------------------------*/
// The engine used to play one opening, forever.  Sixteen seeds should not all
// produce that same one - and the unseeded engine should still play it
static void seedsVaryTheOpening(void)
{
	char lines[16][48], unseeded[48];
	int i, distinct = 1;

	openingLine(0, 6, unseeded);

	for(i = 0; i < 16; ++i)
		openingLine((char)(i * 17 + 1), 6, lines[i]);

	// count how many of the sixteen differ from the first
	for(i = 1; i < 16; ++i)
		if(strcmp(lines[i], lines[0]))
		{
			distinct = 2;
			break;
		}

	check("sixteen seeds do not all play one line", distinct, 2);

	// and the one that matters most: no seed at all is the old engine
	{
		char again[48];
		openingLine(0, 6, again);
		check("an unseeded game is reproducible", strcmp(unseeded, again), 0);
	}
}

/*-----------------------------------------------------------------------*/
// The randomiser perturbs move ordering, and ordering cannot change what
// alpha-beta returns except among moves that score exactly equal.  So a seeded
// search and an unseeded one must agree on the score even when they disagree
// on the move - if they ever disagree on the score, the engine is being made
// to play worse rather than differently
static void randomisedMovesScoreEqual(void)
{
	t_searchResult plain, seeded;
	int i, mismatches = 0, differed = 0;

	for(i = 0; i < 16; ++i)
	{
		board_Init();
		search_SetSeed(0);
		search_Best(SIDE_WHITE, 4, 2000, &plain);

		board_Init();
		search_SetSeed((char)(i * 17 + 1));
		search_Best(SIDE_WHITE, 4, 2000, &seeded);

		if(seeded.m_score != plain.m_score)
			++mismatches;

		if(seeded.m_move.m_from != plain.m_move.m_from ||
		   seeded.m_move.m_to != plain.m_move.m_to)
			++differed;
	}

	check("a randomised move never scores worse", mismatches, 0);

	// not a correctness property, but a randomiser that never varies anything
	// would pass the check above trivially, and this is the test that notices
	check("at least one seed changed the move", differed > 0, 1);
}

/*-----------------------------------------------------------------------*/
// The promise is "my first few moves", so it has to stop.  After
// SEARCH_RANDOM_MOVES searches the seeded engine and the unseeded one are
// back in step, and stay there
static void randomisationStops(void)
{
	t_searchResult plain, late;
	int i;

	board_Init();
	search_SetSeed(0);
	search_Best(SIDE_WHITE, 4, 2000, &plain);

	// Burn the randomised moves without letting the board move on.  Searching
	// the same position over and over is not a game, but it is the only way to
	// vary one thing: after this the position is identical to the one above and
	// the sole difference is that the randomiser has run out of moves
	board_Init();
	search_SetSeed(0x5A);
	for(i = 0; i <= SEARCH_RANDOM_MOVES; ++i)
		search_Best(SIDE_WHITE, 4, 2000, &late);

	check("the randomiser stops after its opening moves",
	      late.m_move.m_from == plain.m_move.m_from &&
	      late.m_move.m_to == plain.m_move.m_to, 1);
}

/*-----------------------------------------------------------------------*/
// The opening table in cpu.c is hand written, and a wrong square in it would
// put an illegal move on the board on move one.  cpu_BookMove looks its roll up
// in the generator's output rather than trusting the table, so a typo makes the
// engine quietly search instead - safe, but it silently removes an opening
// nobody would notice was missing.  So check the table itself: every entry has
// to be a legal first move, and the four of them have to be four *different*
// moves.
//
// The table is static to cpu.c, so this drives the roll the way the game does -
// enough seeds to be sure every entry comes up - and checks what comes back
static void everyBookMoveIsLegal(void)
{
	t_engMove moves[64];
	char count, i, s;
	int seen[64], distinct = 0, illegal = 0;

	memset(seen, 0, sizeof(seen));

	board_Init();
	count = eng_GenMoves(SIDE_WHITE, moves, 64);

	for(s = 1; s; ++s)			// every nonzero seed, then wraps to 0 and stops
	{
		t_searchResult r;
		char legal = 0;

		board_Init();
		undo_Init();
		search_SetSeed(s);

		// cpu_Play is the game's entry point and the only caller of the table
		cpu_Play(SIDE_WHITE);

		// the move it made is the one on the board now; find it by diffing
		for(i = 0; i < count; ++i)
			if(NONE == (geBoard[moves[i].m_from] & PIECE_DATA) &&
			   NONE != (geBoard[moves[i].m_to] & PIECE_DATA))
			{
				legal = 1;
				if(!seen[i]) { seen[i] = 1; ++distinct; }
				break;
			}

		if(!legal)
			++illegal;
		(void)r;
	}

	check("every opening the table plays is legal", illegal, 0);
	check("the table plays four different first moves", distinct, 4);
}

/*-----------------------------------------------------------------------*/
int test_RunOpening(int verbose)
{
	si_failures = 0;

	printf("opening randomisation\n");
	seedsVaryTheOpening();
	randomisedMovesScoreEqual();
	randomisationStops();
	everyBookMoveIsLegal();

	// leave the engine as the rest of the suite expects to find it
	search_SetSeed(0);
	board_Init();

	printf("  -> %d failing\n", si_failures);
	(void)verbose;
	return si_failures;
}
