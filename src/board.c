/*
 *	board.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "engine.h"
#include "search.h"
#include "undo.h"
#include "board.h"

/*-----------------------------------------------------------------------*/
char	gMoveTiles[MAX_PIECE_MOVES];
char	gNumMoveTiles;

// Scratch for one square's attackers.  A side can never have more than its
// sixteen pieces bearing on one tile
static char	sc_attackers[16];

/*-----------------------------------------------------------------------*/
void board_Init(void)
{
	char tile;

	for(tile = 0; tile < 64; ++tile)
	{
		giAttackBoardOffset[tile][SIDE_BLACK] = tile << 1;
		giAttackBoardOffset[tile][SIDE_WHITE] = (tile << 1) + 1;
	}

	eng_SetStartPosition();

	// Not part of the board but does need resetting for every game
	gCursorPos[SIDE_BLACK][1] = gCursorPos[SIDE_WHITE][1] = 4;
	gCursorPos[SIDE_BLACK][0] = 0;
	gCursorPos[SIDE_WHITE][0] = 7;

	gNumMoveTiles = 0;
	board_SyncDisplay();
}

/*-----------------------------------------------------------------------*/
char board_AttackersOf(char tile, char side, char *tiles)
{
	char count = eng_AttackersOf(ENG_FROM_TILE(tile), side, tiles), i;

	// hand them back in the tile numbers the display works in
	for(i = 0; i < count; ++i)
		tiles[i] = ENG_TO_TILE(tiles[i]);

	return count;
}

/*-----------------------------------------------------------------------*/
// Only done when the whole-board display is on, because it is 128 ray-casts
// and there is no reason to pay for it otherwise
static void board_RefreshAttackCounts(void)
{
	char tile, sq;

	for(tile = 0; tile < 64; ++tile)
	{
		sq = ENG_FROM_TILE(tile);
		gpAttackBoard[giAttackBoardOffset[tile][SIDE_BLACK]] =
			eng_AttackersOf(sq, SIDE_BLACK, sc_attackers);
		gpAttackBoard[giAttackBoardOffset[tile][SIDE_WHITE]] =
			eng_AttackersOf(sq, SIDE_WHITE, sc_attackers);
	}
}

/*-----------------------------------------------------------------------*/
void board_SyncDisplay(void)
{
	char sq, tile;

	for(sq = 0; sq < 0x78; ++sq)
	{
		if(ENG_OFFBOARD(sq))
			continue;

		tile = ENG_TO_TILE(sq);
		gChessBoard[tile >> 3][tile & 7] = geBoard[sq];
	}

	if(gShowAttackBoard)
		board_RefreshAttackCounts();
}

/*-----------------------------------------------------------------------*/
// Legal, not pseudo-legal: a move that leaves the king in check is not shown
// to the player as an option at all, so there is no "Invalid" to report any
// more.  A promotion collapses to a single destination here - which piece it
// becomes is asked separately
void board_LegalMovesFrom(char tile)
{
	t_engMove moves[MAX_PIECE_MOVES + 4];
	t_engUndo undo;
	char from = ENG_FROM_TILE(tile);
	char piece = geBoard[from];
	char side, count, i;

	gNumMoveTiles = 0;

	if(NONE == (piece & PIECE_DATA))
		return;

	side = (piece & PIECE_WHITE) >> 7;
	count = eng_GenMovesFrom(from, side, moves, MAX_PIECE_MOVES + 4);

#if ENGINE_FAST_LEGAL
	{
		char wasInCheck = eng_InCheck(side);

		for(i = 0; i < count; ++i)
		{
			char to;

			eng_Make(&moves[i], &undo);
			to = eng_LeavesInCheck(side, &moves[i], wasInCheck)
				? NULL_TILE : ENG_TO_TILE(moves[i].m_to);
			eng_Unmake(&moves[i], &undo);

			if(NULL_TILE == to)
				continue;

			if(!board_findInList(gMoveTiles, gNumMoveTiles, to) &&
			   gNumMoveTiles < MAX_PIECE_MOVES)
				gMoveTiles[gNumMoveTiles++] = to;
		}
	}
#else
	for(i = 0; i < count; ++i)
	{
		char to;

		eng_Make(&moves[i], &undo);
		to = eng_IsAttacked(geKing[side], 1 - side) ? NULL_TILE : ENG_TO_TILE(moves[i].m_to);
		eng_Unmake(&moves[i], &undo);

		if(NULL_TILE == to)
			continue;

		// the four promotions share one destination
		if(!board_findInList(gMoveTiles, gNumMoveTiles, to) &&
		   gNumMoveTiles < MAX_PIECE_MOVES)
			gMoveTiles[gNumMoveTiles++] = to;
	}
#endif
}

/*-----------------------------------------------------------------------*/
char board_IsPromotion(char fromTile, char toTile)
{
	t_engMove moves[MAX_PIECE_MOVES + 4];
	char from = ENG_FROM_TILE(fromTile), to = ENG_FROM_TILE(toTile);
	char piece = geBoard[from];
	char count, i;

	if(PAWN != (piece & PIECE_DATA))
		return 0;

	count = eng_GenMovesFrom(from, (piece & PIECE_WHITE) >> 7, moves, MAX_PIECE_MOVES + 4);
	for(i = 0; i < count; ++i)
		if(moves[i].m_to == to && (moves[i].m_flags & ENG_MF_PROMO))
			return 1;

	return 0;
}

/*-----------------------------------------------------------------------*/
char board_FindMove(char fromTile, char toTile, char promote, t_engMove *move)
{
	t_engMove moves[MAX_PIECE_MOVES + 4];
	char from = ENG_FROM_TILE(fromTile), to = ENG_FROM_TILE(toTile);
	char piece = geBoard[from];
	char count, i;

	if(NONE == (piece & PIECE_DATA))
		return 0;

	count = eng_GenMovesFrom(from, (piece & PIECE_WHITE) >> 7, moves, MAX_PIECE_MOVES + 4);

	for(i = 0; i < count; ++i)
	{
		if(moves[i].m_from != from || moves[i].m_to != to)
			continue;
		if((moves[i].m_flags & ENG_MF_PROMO) && promote != (moves[i].m_flags & ENG_MF_PROMO))
			continue;

		*move = moves[i];
		return 1;
	}

	return 0;
}

/*-----------------------------------------------------------------------*/
char board_ApplyMove(const t_engMove *move, char side)
{
	t_engUndo undo;
	char outcome, other = 1 - side;

	eng_Make(move, &undo);

	// Check, mate and stalemate all come out of one question now: does the
	// other side have a legal move, and is it in check
	outcome = search_Outcome(other);

	if(OUTCOME_OK == outcome)
	{
		if(eng_InCheck(other))
			outcome = OUTCOME_CHECK;
		else if(geHalfmove >= (NUM_MOVES_TO_DRAW * 2))
			outcome = OUTCOME_DRAW;
		// the threefold a real game is drawn by: twice before plus now.  The
		// search settles for one repeat, but a game should not end on the
		// board until the rule actually says it has
		else if(eng_IsRepetition(2))
			outcome = OUTCOME_DRAW;
	}

	undo_AddMove(move, &undo, side, outcome);
	board_SyncDisplay();

	return outcome;
}

/*-----------------------------------------------------------------------*/
// Utility function to find a number in a series of numbers
char board_findInList(char *list, char numElements, char number)
{
	while(numElements--)
	{
		if(number == list[numElements])
			return 1;
	}
	return 0;
}
