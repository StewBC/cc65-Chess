/*
 *	plat.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _PLAT_H_
#define _PLAT_H_

void plat_Init(void);
void plat_UpdateScreen(void);
char plat_Menu(char **menuItems, char height, char *scroller);
void plat_DrawBoard(char clearLog);
void plat_DrawSquare(char position);
void plat_ShowSideToGoLabel(char side);
void plat_Highlight(char position, char color, char cursor);
void plat_ShowMessage(char *str, char color);
void plat_ClearMessage(void);
void plat_AddToLogWin(void);
void plat_AddToLogWinTop(void);
int plat_ReadKeys(char blocking);
void plat_Shutdown(void);

// One byte of entropy, read once a game, for the opening randomiser.  Every
// machine here has a free running counter of some kind and cc65's asminc names
// all of them, so no port has to guess an address.  A byte is enough: it seeds
// an 8 bit LFSR, and the randomiser only ever picks between a handful of moves
// the search has already scored equal.
//
// This is the one addition to this file since it was frozen, and it is only
// worth it because it costs each port three lines and cannot fail visibly - a
// bad seed makes the openings repeat, which is exactly what happens now.
char plat_GetSeed(void);

#endif //_PLAT_H_
