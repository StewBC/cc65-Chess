/*
 *	board.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 *	What used to be the move generator and the attack database is now the
 *	adapter between the engine and the display: it keeps gChessBoard and the
 *	attacker counts in step for the platform files, turns cursor tiles into
 *	engine moves, and applies a move together with its undo entry.
 *
 *	Everything here speaks the UI's 0..63 tile numbers.  0x88 stays inside the
 *	engine.
 */

#ifndef _BOARD_H_
#define _BOARD_H_

#include "engine.h"

/*-----------------------------------------------------------------------*/
// Destination tiles for the piece the cursor is on, as 0..63
extern char		gMoveTiles[MAX_PIECE_MOVES];
extern char		gNumMoveTiles;

/*-----------------------------------------------------------------------*/
void board_Init(void);

// Refresh gChessBoard from the engine, and the attacker counts with it when
// the board display is switched on.  Called after anything that moves a piece
void board_SyncDisplay(void);

// The A and D displays ask for this directly, so it is not folded into the
// sync - it is only ever wanted for one tile at a time
char board_AttackersOf(char tile, char side, char *tiles);

/*-----------------------------------------------------------------------*/
// Fill gMoveTiles with every legal destination for the piece on "tile"
void board_LegalMovesFrom(char tile);

// Turn a from/to pair the cursor produced back into the engine's move.
// "promote" is the piece a pawn reaching the back rank becomes, or NONE
char board_FindMove(char fromTile, char toTile, char promote, t_engMove *move);

// Make the move, push it onto the undo stack and return the outcome
char board_ApplyMove(const t_engMove *move, char side);

// Does this move need the player to be asked what to promote to
char board_IsPromotion(char fromTile, char toTile);

/*-----------------------------------------------------------------------*/
char board_findInList(char *list, char numElements, char number);

#endif //_BOARD_H_
