/*
 *	board.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _BOARD_H_
#define _BOARD_H_

void board_Init();
void board_PlacePieceAttacks();
void board_GeneratePossibleMoves(char position, char addDefenceMove);
char board_ProcessAction(void);
void board_ProcessEnPassant(char state);
void board_ProcessCastling(char a, char b);
char board_CheckForMate(char side);
char board_findInList(char *list, char numElements, char number);

#endif //_BOARD_H_