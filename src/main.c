/*
 *	main.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "undo.h"
#include "board.h"
#include "cpu.h"
#include "human.h"
#include "frontend.h"
#include "search.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
// Internal function Prototype
void mainLoop(void);
void init(void);

/*-----------------------------------------------------------------------*/
int main()
{
	init();
	mainLoop();
	plat_Shutdown();
	
	return 0;
}

/*-----------------------------------------------------------------------*/
void init()
{
	
	// Init the global variables that aren't initialized anywhere else
	// (mostly other *_Init() functions, or in mainLoop)
	gTile[0] = gTile[1] = NULL_TILE;
	gLogStrBuffer[6] = gShowAttacks[0] = gShowAttacks[1] = gShowAttackBoard =
		gPiece[0] = gPiece[1] = gOutcome = gColor[0] = gColor[1] = 0;
	gSkillLevel = 0;

	plat_Init();
}

/*-----------------------------------------------------------------------*/
void mainLoop()
{
	char activeGame, sideToGo, outcome;
	
	do
	{
		// Execute once for every game
		board_Init();
		undo_Init();
		plat_DrawBoard(1);
		
		gUserMode = 0;
		activeGame = 0;
		sideToGo = SIDE_WHITE;
		outcome = OUTCOME_MENU;
		
		while(outcome <= OUTCOME_MENU)
		{
			// Allows interruption of AI vs AI
			if(INPUT_MENU & plat_ReadKeys(0))
				outcome = OUTCOME_MENU;
			
			if(OUTCOME_MENU == outcome)
			{
				outcome = frontend_Menu(activeGame);
				if(outcome < OUTCOME_ABANDON)
				{
					// Seed the opening randomiser here, and only here.  This is
					// the first instant in a cold boot at which anything has
					// happened that the machine could not have predicted - the
					// human has just walked the menu, which takes an
					// unrepeatable number of jiffies.  Seeding at board_Init
					// instead would read the same counter value every time the
					// machine is switched on.
					// !activeGame makes it once a game rather than once a menu
					if(!activeGame)
						search_SetSeed(plat_GetSeed());
					activeGame = 1;
				}
			}
			
			if(outcome <= OUTCOME_STALEMATE)
			{
				plat_ShowSideToGoLabel(sideToGo);
				
				if((sideToGo+1) & gUserMode)
					outcome = human_Play(sideToGo);
				else
					outcome = cpu_Play(sideToGo);
					
				if(gShowAttackBoard)
					plat_DrawBoard(0);

				// Only switch sides if not coming from a menu and it's not STALEMATE
				if(outcome != OUTCOME_MENU && outcome != OUTCOME_STALEMATE)
					sideToGo = 1 - sideToGo;

				// if it's game-over then make it a USER vs USER state so control
				// returns no matter which side should have gone next
				if(outcome >= OUTCOME_CHECKMATE)
					gUserMode = USER_BLACK | USER_WHITE;

				// Any platforms that need to redraw should do so now
				plat_UpdateScreen();
			}
		}
	} while(OUTCOME_QUIT != outcome);
}
