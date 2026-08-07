/*
 *	castle.c
 *	cc65 Chess - test support
 *
 *	Castling and en passant rules, carried over from the old suite and pointed
 *	at the new engine.  These are the cases that were actually wrong once, so
 *	they earn their keep.
 *
 *	The question asked is whether a castle is *offered*, because the UI only
 *	ever lets a player pick from generated moves.  The one exception is the
 *	square the king lands on, which the generator leaves to the ordinary
 *	king-in-check filter.
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
		printf("    %-48s got %d, wanted %d\n", what, got, want);
		++si_failures;
	}
}

/*-----------------------------------------------------------------------*/
// Legal destinations from a square, which is what the cursor offers
static int offered(const char *from, const char *to)
{
	board_LegalMovesFrom(test_Square(from));
	return board_findInList(gMoveTiles, gNumMoveTiles, test_Square(to));
}

/*-----------------------------------------------------------------------*/
static void put(const char *square, char piece, char white)
{
	char sq = ENG_FROM_TILE(test_Square(square));

	geBoard[sq] = piece | (white ? PIECE_WHITE : 0);
	if(KING == piece)
		geKing[white ? SIDE_WHITE : SIDE_BLACK] = sq;
}

static void position(void) { eng_Clear(); }

/*-----------------------------------------------------------------------*/
static void castleRules(void)
{
	position();
	put("e1", KING, 1); put("a1", ROOK, 1); put("h1", ROOK, 1);
	put("b1", KNIGHT, 0); put("e8", KING, 0);
	geCastle = ENG_CASTLE_WK | ENG_CASTLE_WQ;
	check("enemy knight on b1: O-O-O not offered", offered("e1", "c1"), 0);
	check("enemy knight on b1: O-O still offered", offered("e1", "g1"), 1);

	position();
	put("e1", KING, 1); put("h1", ROOK, 1);
	put("e8", ROOK, 0); put("a8", KING, 0);
	geCastle = ENG_CASTLE_WK;
	check("king in check: O-O not offered", offered("e1", "g1"), 0);

	position();
	put("e1", KING, 1); put("h1", ROOK, 1);
	put("f8", ROOK, 0); put("a8", KING, 0);
	geCastle = ENG_CASTLE_WK;
	check("crossed tile f1 attacked: O-O not offered", offered("e1", "g1"), 0);

	position();
	put("e1", KING, 1); put("h1", ROOK, 1);
	put("g8", ROOK, 0); put("a8", KING, 0);
	geCastle = ENG_CASTLE_WK;
	check("landing tile g1 attacked: O-O not offered", offered("e1", "g1"), 0);

	position();
	put("e1", KING, 1); put("a1", ROOK, 1); put("h1", ROOK, 1);
	put("d8", ROOK, 0); put("a5", KING, 0);
	geCastle = ENG_CASTLE_WK | ENG_CASTLE_WQ;
	check("crossed tile d1 attacked: O-O-O not offered", offered("e1", "c1"), 0);
	check("crossed tile d1 attacked: O-O still offered", offered("e1", "g1"), 1);

	position();
	put("e1", KING, 1); put("a1", ROOK, 1); put("h1", ROOK, 1);
	put("b8", ROOK, 0); put("a5", KING, 0);
	geCastle = ENG_CASTLE_WK | ENG_CASTLE_WQ;
	check("b1 attacked but empty: O-O-O offered", offered("e1", "c1"), 1);

	position();
	put("e1", KING, 1); put("a1", ROOK, 1); put("h1", ROOK, 1);
	put("e8", KING, 0);
	geCastle = ENG_CASTLE_WQ;		// kingside right already lost
	check("kingside right lost: O-O not offered", offered("e1", "g1"), 0);
	check("kingside right lost: O-O-O still offered", offered("e1", "c1"), 1);

	position();
	put("e1", KING, 1); put("a1", ROOK, 1); put("h1", ROOK, 1);
	put("e8", KING, 0);
	geCastle = ENG_CASTLE_WK | ENG_CASTLE_WQ;
	check("clear board: O-O offered", offered("e1", "g1"), 1);
	check("clear board: O-O-O offered", offered("e1", "c1"), 1);
	check("black has no rook: O-O not offered", offered("e8", "g8"), 0);
}

/*-----------------------------------------------------------------------*/
// Moving the king or a rook has to give up the matching right, and so does
// having a rook captured on its home square
static void castleRights(void)
{
	t_engMove move;

	position();
	put("e1", KING, 1); put("a1", ROOK, 1); put("h1", ROOK, 1);
	put("e8", KING, 0); put("a8", ROOK, 0); put("h8", ROOK, 0);
	geCastle = ENG_CASTLE_ALL;
	board_SyncDisplay();
	undo_Init();

	check("h1 rook moves: kingside right gone",
	      board_FindMove(test_Square("h1"), test_Square("h5"), NONE, &move) &&
	      (board_ApplyMove(&move, SIDE_WHITE), 0 == (geCastle & ENG_CASTLE_WK)), 1);
	check("h1 rook moves: queenside right kept", 0 != (geCastle & ENG_CASTLE_WQ), 1);

	undo_Undo();
	check("undo restores the kingside right", 0 != (geCastle & ENG_CASTLE_WK), 1);

	check("king moves: both white rights gone",
	      board_FindMove(test_Square("e1"), test_Square("e2"), NONE, &move) &&
	      (board_ApplyMove(&move, SIDE_WHITE),
	       0 == (geCastle & (ENG_CASTLE_WK|ENG_CASTLE_WQ))), 1);
	check("king moves: black rights untouched",
	      (geCastle & (ENG_CASTLE_BK|ENG_CASTLE_BQ)) == (ENG_CASTLE_BK|ENG_CASTLE_BQ), 1);

	undo_Undo();
	check("undo restores both king rights",
	      (geCastle & (ENG_CASTLE_WK|ENG_CASTLE_WQ)) == (ENG_CASTLE_WK|ENG_CASTLE_WQ), 1);

	// a rook taken on its home square loses the right just the same
	// the capturing rook has to be able to reach h8 - down the open h file,
	// not along the back rank where the black king is in the way
	position();
	put("e1", KING, 1); put("h1", ROOK, 1);
	put("e8", KING, 0); put("h8", ROOK, 0); put("a8", ROOK, 0);
	geCastle = ENG_CASTLE_ALL;
	undo_Init();
	check("rook captured on h8: black kingside right gone",
	      board_FindMove(test_Square("h1"), test_Square("h8"), NONE, &move) &&
	      (board_ApplyMove(&move, SIDE_WHITE), 0 == (geCastle & ENG_CASTLE_BK)), 1);
	check("rook captured on h8: black queenside right kept",
	      0 != (geCastle & ENG_CASTLE_BQ), 1);
}

/*-----------------------------------------------------------------------*/
static void castleMechanics(void)
{
	t_engMove move;
	char before[128];

	position();
	put("e1", KING, 1); put("h1", ROOK, 1); put("a1", ROOK, 1);
	put("e8", KING, 0);
	geCastle = ENG_CASTLE_WK | ENG_CASTLE_WQ;
	board_SyncDisplay();
	undo_Init();
	memcpy(before, geBoard, 128);

	check("O-O found", board_FindMove(test_Square("e1"), test_Square("g1"), NONE, &move), 1);
	board_ApplyMove(&move, SIDE_WHITE);
	check("king on g1", KING == (geBoard[ENG_FROM_TILE(test_Square("g1"))] & PIECE_DATA), 1);
	check("rook on f1", ROOK == (geBoard[ENG_FROM_TILE(test_Square("f1"))] & PIECE_DATA), 1);
	check("h1 empty", NONE == (geBoard[ENG_FROM_TILE(test_Square("h1"))] & PIECE_DATA), 1);
	check("king tracker follows", geKing[SIDE_WHITE], ENG_FROM_TILE(test_Square("g1")));
	check("display mirror shows the rook",
	      ROOK == (gChessBoard[7][5] & PIECE_DATA), 1);

	undo_Undo();
	check("undo of O-O restores the position", 0 == memcmp(before, geBoard, 128), 1);
	check("king tracker restored", geKing[SIDE_WHITE], ENG_FROM_TILE(test_Square("e1")));

	undo_Redo();
	check("redo puts the rook back on f1",
	      ROOK == (geBoard[ENG_FROM_TILE(test_Square("f1"))] & PIECE_DATA), 1);
}

/*-----------------------------------------------------------------------*/
static void enPassant(void)
{
	t_engMove move;
	char before[128];

	position();
	put("e1", KING, 1); put("e8", KING, 0);
	put("e5", PAWN, 1); put("d7", PAWN, 0);
	undo_Init();

	check("d7-d5 found", board_FindMove(test_Square("d7"), test_Square("d5"), NONE, &move), 1);
	board_ApplyMove(&move, SIDE_BLACK);
	check("en passant target is d6", geEP, ENG_FROM_TILE(test_Square("d6")));
	check("e5xd6 is offered", offered("e5", "d6"), 1);

	memcpy(before, geBoard, 128);
	check("e5xd6 found", board_FindMove(test_Square("e5"), test_Square("d6"), NONE, &move), 1);
	board_ApplyMove(&move, SIDE_WHITE);
	check("the d5 pawn was taken",
	      NONE == (geBoard[ENG_FROM_TILE(test_Square("d5"))] & PIECE_DATA), 1);

	undo_Undo();
	check("undo of en passant restores exactly", 0 == memcmp(before, geBoard, 128), 1);
	check("undo restores the en passant target", geEP, ENG_FROM_TILE(test_Square("d6")));
}

/*-----------------------------------------------------------------------*/
static void promotion(void)
{
	t_engMove move;

	position();
	put("e1", KING, 1); put("a8", KING, 0);
	put("b7", PAWN, 1);
	undo_Init();

	check("promotion is flagged", board_IsPromotion(test_Square("b7"), test_Square("b8")), 1);
	check("a plain pawn push is not", board_IsPromotion(test_Square("e1"), test_Square("e2")), 0);

	check("can promote to a knight",
	      board_FindMove(test_Square("b7"), test_Square("b8"), KNIGHT, &move), 1);
	board_ApplyMove(&move, SIDE_WHITE);
	check("a knight arrived", KNIGHT == (geBoard[ENG_FROM_TILE(test_Square("b8"))] & PIECE_DATA), 1);

	undo_Undo();
	check("undo makes it a pawn again",
	      PAWN == (geBoard[ENG_FROM_TILE(test_Square("b7"))] & PIECE_DATA), 1);
}

/*-----------------------------------------------------------------------*/
// The B overlay reads gpAttackBoard[giAttackBoardOffset[tile][side]] directly
// in every port, so the counts have to be right and have to refresh
static void displayCounts(void)
{
	board_Init();
	gShowAttackBoard = 1;
	board_SyncDisplay();

	// In the opening position e2 is defended by the king, the queen, the
	// light bishop and the king's knight - four, and nothing of black's
	check("white defenders of e2 = 4",
	      gpAttackBoard[giAttackBoardOffset[test_Square("e2")][SIDE_WHITE]], 4);
	check("black attackers of e2 = 0",
	      gpAttackBoard[giAttackBoardOffset[test_Square("e2")][SIDE_BLACK]], 0);
	check("black defenders of e7 = 4",
	      gpAttackBoard[giAttackBoardOffset[test_Square("e7")][SIDE_BLACK]], 4);

	// e4 is out of reach in the opening: pawns take diagonally, the knights
	// are still home and the queen's diagonal is blocked
	check("white attackers of e4 = 0",
	      gpAttackBoard[giAttackBoardOffset[test_Square("e4")][SIDE_WHITE]], 0);
	// a3 is reached by the b2 pawn and by the b1 knight
	check("white attackers of a3 = 2",
	      gpAttackBoard[giAttackBoardOffset[test_Square("a3")][SIDE_WHITE]], 2);

	// and the A/D lists agree with the counts
	{
		char list[16];
		check("attackersOf agrees with the count",
		      board_AttackersOf(test_Square("e2"), SIDE_WHITE, list), 4);
		check("one of them is the king on e1",
		      board_findInList(list, 4, test_Square("e1")), 1);
	}

	gShowAttackBoard = 0;
}

/*-----------------------------------------------------------------------*/
int test_RunCastle(int verbose)
{
	(void)verbose;
	si_failures = 0;

	castleRules();
	castleRights();
	castleMechanics();
	enPassant();
	promotion();
	displayCounts();

	printf("castle / en passant / promotion: %d failing\n", si_failures);
	return si_failures;
}
