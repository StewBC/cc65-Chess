/*
 *	repetition.c
 *	cc65 Chess - test support
 *
 *	Repetition detection, which is the one piece of engine state that is not
 *	visible on the board.  The fuzzer already checks that the running hash
 *	agrees with a recount of the pieces; what it cannot check is the history
 *	behind it, and every interesting failure lives there rather than in the
 *	key: a history that outlives the game it belongs to, a scan that walks
 *	past a capture, or two positions with the same pieces that are not the
 *	same position.
 *
 *	The pair that matters most is kingShuffle against rightsShuffle.  They
 *	play the identical four moves and end with the identical pieces on the
 *	identical squares, and exactly one of them is a repetition - because in
 *	the second, the king that walked out and back gave up the right to castle
 *	on the way.  That is the case a hash of the piece placement alone gets
 *	wrong, and it is a shuffle, which is precisely the shape of position this
 *	whole feature exists to judge.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "undo.h"
#include "board.h"
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
// Play a move by its squares, the way a player would, through the same path
// the game uses so that the history is built the way a real game builds it
static int play(const char *from, const char *to)
{
	t_engMove move;
	char fromTile = test_Square(from), toTile = test_Square(to);
	char side = (geBoard[ENG_FROM_TILE(fromTile)] & PIECE_WHITE) ? SIDE_WHITE : SIDE_BLACK;

	if(!board_FindMove(fromTile, toTile, NONE, &move))
	{
		printf("    no move %s%s available\n", from, to);
		++si_failures;
		return OUTCOME_INVALID;
	}
	return board_ApplyMove(&move, side);
}

/*-----------------------------------------------------------------------*/
// Four knight moves and back: the cheapest real repetition there is
static void knightShuffle(void)
{
	eng_SetStartPosition();
	undo_Init();

	check("start position is not yet a repetition", eng_IsRepetition(1), 0);

	play("g1", "f3"); play("g8", "f6");
	check("halfway back, still no repetition", eng_IsRepetition(1), 0);

	play("f3", "g1"); play("f6", "g8");
	check("back to the start, one repetition", eng_IsRepetition(1), 1);
	check("but not yet a threefold", eng_IsRepetition(2), 0);

	// round two of the same dance reaches the position a third time
	play("g1", "f3"); play("g8", "f6");
	play("f3", "g1");
	check("round two, the move that completes it is drawn",
	      play("f6", "g8"), OUTCOME_DRAW);
	check("round two, now a threefold", eng_IsRepetition(2), 1);
}

/*-----------------------------------------------------------------------*/
// The same four moves with a king, from a position where there is nothing to
// give up.  This has to be a repetition, or the test below proves nothing
static void kingShuffle(void)
{
	test_EngineSetFEN("4k3/8/8/8/8/8/8/4K2R w - - 0 1");
	undo_Init();

	play("e1", "e2"); play("e8", "e7");
	play("e2", "e1"); play("e7", "e8");
	check("king walked out and back, no rights at stake", eng_IsRepetition(1), 1);
}

/*-----------------------------------------------------------------------*/
// Identical pieces on identical squares, and not the same position: the king
// could castle before it moved and cannot now
static void rightsShuffle(void)
{
	test_EngineSetFEN("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
	undo_Init();

	play("e1", "e2"); play("e8", "e7");
	play("e2", "e1"); play("e7", "e8");
	check("same pieces, lost the castling right, not a repetition",
	      eng_IsRepetition(1), 0);
}

/*-----------------------------------------------------------------------*/
// A capture makes everything before it unreachable, so the scan must stop
// there even though the ring still holds those positions
static void captureCutsHistory(void)
{
	test_EngineSetFEN("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
	undo_Init();

	play("e4", "d5");			// pawn takes pawn, fifty move counter to zero
	check("fifty move counter reset by the capture", geHalfmove, 0);
	check("nothing to repeat across a capture", eng_IsRepetition(1), 0);

	play("e8", "d8"); play("e1", "d1");
	play("d8", "e8"); play("d1", "e1");
	check("a shuffle after the capture is still seen", eng_IsRepetition(1), 1);
}

/*-----------------------------------------------------------------------*/
// A FEN can arrive with a fifty move counter and no history behind it.  The
// scan is bounded by what has actually been pushed, not by what the counter
// claims, or it reads whatever the last game left in the ring
static void loadedPositionHasNoHistory(void)
{
	// play a game first, so the ring is full of somebody else's positions
	eng_SetStartPosition();
	undo_Init();
	play("g1", "f3"); play("g8", "f6");
	play("f3", "g1"); play("f6", "g8");
	check("the previous game did repeat", eng_IsRepetition(1), 1);

	// the test FEN parser stops at the en passant field, so the counter goes
	// on by hand - which is the point of the check.  A high counter with an
	// empty history is exactly the state that would send the scan walking
	// back into the game above
	test_EngineSetFEN("4k3/8/8/8/8/8/8/4K3 w - - 40 60");
	geHalfmove = 40;
	check("a loaded position starts with a clean history", eng_IsRepetition(1), 0);
}

/*-----------------------------------------------------------------------*/
int test_RunRepetition(int verbose)
{
	si_failures = 0;

	printf("repetition detection\n");
	knightShuffle();
	kingShuffle();
	rightsShuffle();
	captureCutsHistory();
	loadedPositionHasNoHistory();

	printf("  -> %d failing\n", si_failures);
	(void)verbose;
	return si_failures;
}
