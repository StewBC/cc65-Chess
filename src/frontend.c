/*
 *	frontend.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "plat.h"
#include "frontend.h"

/*-----------------------------------------------------------------------*/
// All menu's in the games are the same height
#define MENU_HEIGHT 6

/*-----------------------------------------------------------------------*/
// Show the main menu and deal with the selection.	If a confirmation or
// side-selection is needed, show that and deal with that oucome as well.
char frontend_Menu(char activeGame)
{
	char outcome = OUTCOME_MENU;

	// If there's a game in progress, show the "Resume" option
	if(activeGame)
	{
		gMainMenu[4] = gszQuit;
		gMainMenu[5] = gszResume;
	}
	else
	{
		// if no game in progress, only show the quit (to dos) option
		// if okay to do so
		gMainMenu[4+gReturnToOS] = 0;
	}
		
	while(OUTCOME_MENU == outcome)
	{
		// Show the main menu
		switch(plat_Menu(gMainMenu, MENU_HEIGHT, gszAbout))
		{
			// back-up
			case 0:
				if(activeGame)
					outcome = OUTCOME_OK;
				else
					outcome = OUTCOME_QUIT; 
			break;			

			// 1 player
			case 1:
				// Show the menu allowing the user to choose a side
				switch(plat_Menu(gColorMenu, MENU_HEIGHT, gszAbout))
				{
					case 1:
						gUserMode = USER_WHITE;
						outcome = OUTCOME_OK;
					break;			
						
					case 2:
						gUserMode = USER_BLACK;
						outcome = OUTCOME_OK;
					break;			
				}
			break;
			
			// 2 human players
			case 2:
				gUserMode = USER_BLACK | USER_WHITE;
				outcome = OUTCOME_OK;
			break;			
				
			// Both players AI
			case 3:
				gUserMode = 0;
				outcome = OUTCOME_OK;
			break;			
			
			// Quit 
			case 4:
				outcome = OUTCOME_QUIT;
			break;			
			
			// Resume	
			case 5:
				outcome = OUTCOME_OK;
			break;			
		}
		if(OUTCOME_QUIT == outcome)
		{
			// Get confirmation of quit
			if(1 != plat_Menu(gAreYouSureMenu, MENU_HEIGHT, gszAbout))
				outcome = OUTCOME_MENU;
			else if(activeGame)
				outcome = OUTCOME_ABANDON;
		}
		else
		{
			if(gUserMode != 3)
			{
				char skill = plat_Menu(gSkillMenu, MENU_HEIGHT, gszAbout);
				if(skill)
				{
					skill -= 1;
					skill *= 3;
					gWidth = gSkill[skill];
					gMaxLevel = gSkill[skill+1];
					gDeepThoughts = gSkill[skill+2];
				}
				else
					outcome = OUTCOME_MENU;
			}
		}
	}
	// Redraw the whole board, erasing the menu
	plat_DrawBoard(0);
	return outcome;
}

/*-----------------------------------------------------------------------*/
// Show a menu seeing what the user wants to do with the pawn that reached
// the other side
char frontend_GetPromotion()
{
	char promotion;
	
	switch(plat_Menu(gPromoteMenu, MENU_HEIGHT, gszpromote))
	{
		case 2:
		promotion =	 ROOK;
		break;

		case 3:
		promotion =	 BISHOP;
		break;

		case 4:
		promotion =	 KNIGHT;
		break;
		
		default:
		promotion =	 QUEEN;
		break;
	}
	plat_DrawBoard(0);
	return promotion;
}

/*-----------------------------------------------------------------------*/
// This formats the log entry.	It's here because it's display related
void frontend_FormatLogString()
{
	gLogStrBuffer[0] = 'A' + (gTile[0] & 7);
	gLogStrBuffer[1] = '8' - (gTile[0] / 8);
	gLogStrBuffer[2] = (gPiece[1] & PIECE_DATA) != NONE ? 'x' : '-';
	gLogStrBuffer[3] = 'A' + (gTile[1] & 7);
	gLogStrBuffer[4] = '8' - (gTile[1] / 8);
	gLogStrBuffer[5] = gMoveSymbol[gOutcome-1];
}

/*-----------------------------------------------------------------------*/
// Call the platform functions to draw the log update - at the bottom
// or top depending on undo (top) or new/redo (bottom)
// The plat_* functions probably call undo_FindUndoLine which changes
// the global gTile, etc. variables
void frontend_LogMove(char atTop)
{
	if(atTop)
		plat_AddToLogWinTop();
	else
		plat_AddToLogWin();
}

