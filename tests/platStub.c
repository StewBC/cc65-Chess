/*
 *	platStub.c
 *	cc65 Chess - test support
 *
 *	A do-nothing implementation of plat.h so the game logic can be linked and
 *	driven natively.  plat_AddToLogWin deliberately walks the undo stack the
 *	way the real ports do, because that call has the side effect of changing
 *	gTile/gPiece/gColor/gMove and the engine has to survive it
 */

#include "types.h"
#include "globals.h"
#include "undo.h"
#include "frontend.h"
#include "plat.h"

void plat_Init(void) {}
void plat_UpdateScreen(void) {}
void plat_DrawBoard(char clearLog) { (void)clearLog; }
void plat_DrawSquare(char position) { (void)position; }
void plat_ShowSideToGoLabel(char side) { (void)side; }
void plat_Highlight(char position, char color, char cursor) { (void)position; (void)color; (void)cursor; }
void plat_ShowMessage(char *str, char color) { (void)str; (void)color; }
void plat_ClearMessage(void) {}
void plat_Shutdown(void) {}
int  plat_ReadKeys(char blocking) { (void)blocking; return 0; }

/*-----------------------------------------------------------------------*/
// Always picks the first item, which makes frontend_GetPromotion return a
// queen - the same choice the AI path makes
char plat_Menu(char **menuItems, char height, char *scroller)
{
	(void)menuItems; (void)height; (void)scroller;
	return 1;
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWin(void)
{
	char y;

	for(y = 0; y < 20; ++y)
		if(undo_FindUndoLine(y))
			frontend_FormatLogString();
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWinTop(void)
{
	plat_AddToLogWin();
}
