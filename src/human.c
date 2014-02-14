/*
 *	human.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "undo.h"
#include "board.h"
#include "human.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
// Internal function Prototype
void human_ProcessInput(int keyMask);
void human_ProcessToggle(int keyMask, char side, char tile);

/*-----------------------------------------------------------------------*/
// Track the user controlled curson on the board
static char sc_cursorX, sc_cursorY;

/*-----------------------------------------------------------------------*/
// Handle the cursor movement
void human_ProcessInput(int keyMask)
{
	switch(keyMask)
	{
		case INPUT_UP:
			if(!sc_cursorY)
				sc_cursorY = 7;
			else
				--sc_cursorY;
		break;

		case INPUT_RIGHT:
			if(7==sc_cursorX)
				sc_cursorX = 0;
			else
				++sc_cursorX;
		break;

		case INPUT_DOWN:
			if(7==sc_cursorY)
				sc_cursorY = 0;
			else
				++sc_cursorY;
		break;

		case INPUT_LEFT:
			if(!sc_cursorX)
				sc_cursorX = 7;
			else
				--sc_cursorX;
		break;
	}
}

/*-----------------------------------------------------------------------*/
// Deal with the toggling on/off of the attack and defend states
// as well as the 'b'oard state which shows all attcks/defences
void human_ProcessToggle(int keyMask, char side, char tile)
{
	char attack = 0;
	
	switch(keyMask)
	{
		case INPUT_TOGGLE_B:
			gShowAttackBoard = 1 - gShowAttackBoard;
			plat_DrawBoard(0);
		break;
		
		case INPUT_TOGGLE_A:
			attack = 1;
			// Intentional fall-through since the code
			// below is the same for showing attack or defence

		case INPUT_TOGGLE_D:
		{
			int offset;
			char attackers, attacker, i;
			
			gShowAttacks[side] ^= SET_BIT(attack);
			
			offset = giAttackBoardOffset[tile][0];
			if((side & !attack) || (!side && attack))
				offset += ATTACK_WHITE_OFFSET;
			attackers = gpAttackBoard[offset];
			++offset;
			for(i=0;i<attackers;++i)
			{
				attacker = gpAttackBoard[offset+i];
				if(gShowAttacks[side] & SET_BIT(attack))
					plat_Highlight(attacker,2+attack);
				else
					plat_DrawSquare(attacker);
			}
		}
		break;
	}
}

/*-----------------------------------------------------------------------*/
// Main routine when it's a human player's turn.
// The routine is sort-of in 2 parts. The bit before the user key is read
// and the bit after the key is read.
char human_Play(char side)
{
	char validMove = 0, selector = 0;
	int keyMask = INPUT_MOTION;

	// get this sides' cursor
	sc_cursorY = gCursorPos[side][0];
	sc_cursorX = gCursorPos[side][1];
	
	do
	{
		// Load the global variables with the current piece under the cursor
		gTile[selector] = MK_POS(sc_cursorY,sc_cursorX);
		
		gPiece[selector] = gpChessBoard[gTile[selector]];
		gColor[selector] = (gPiece[selector] & PIECE_WHITE) >> 7;
		gMove[selector] = gPiece[selector] & PIECE_MOVED;
		gPiece[selector] &= PIECE_DATA;
		
		if(keyMask & INPUT_MOTION)
		{
			// If no piece selected and the cursor moved, update the possible moves for this tile
			if(!selector)
				board_GeneratePossibleMoves(gTile[0], 0);
			else
			{
				// If a piece is selected, see if the second tile, under the cursor, is
				// a valid move-to tile
				validMove = board_findInList(gPossibleMoves, gNumMoves, gTile[1]);
				plat_Highlight(gTile[1], validMove ? HCOLOR_ATTACK : HCOLOR_INVALID);
			}

			// Show the cursor
			plat_Highlight(gTile[0], selector ? HCOLOR_SELECTED : NONE == gPiece[0] || gColor[0] != side ? HCOLOR_EMPTY : gNumMoves ? HCOLOR_VALID : HCOLOR_INVALID);
		}

		// If the cursor moved and the toggle-show-attackers/defenders states were on for this side,
		// Handle showing them for the now selected tile. Bit 2/3 says it was on, so toggle it on.
		// 2/3 is set whem the selection changes, lower in this same routine
		if(gShowAttacks[side] & SET_BIT(2))
		{
			gShowAttacks[side] &= ~SET_BIT(2);
			human_ProcessToggle(INPUT_TOGGLE_D, side, gTile[selector]);
		}
		
		if(gShowAttacks[side] & SET_BIT(3))
		{
			gShowAttacks[side] &= ~SET_BIT(3);
			human_ProcessToggle(INPUT_TOGGLE_A, side, gTile[selector]);
		}

		// Get input
		keyMask = plat_ReadKeys(1);
		
		// Always clear the message area once a key is pressed
		plat_ClearMessage();
		
		// If the selected tile changes, make sure the toggle-show updates will happen (Set 2/3 bit)
		if(keyMask & (INPUT_MOTION | INPUT_UNDOREDO) || ((keyMask & INPUT_SELECT) && selector && validMove))
		{
			if(gShowAttacks[side] & SET_BIT(0))
			{
				gShowAttacks[side] |= SET_BIT(2);
				human_ProcessToggle(INPUT_TOGGLE_D, side, gTile[selector]);
			}
				
			if(gShowAttacks[side] & SET_BIT(1))
			{
				gShowAttacks[side] |= SET_BIT(3);
				human_ProcessToggle(INPUT_TOGGLE_A, side, gTile[selector]);
			}
		}
		
		if(keyMask & INPUT_MOTION)
		{
			// Erase the cursor and move it
			plat_DrawSquare(gTile[selector]);
			human_ProcessInput(keyMask & INPUT_MOTION);
		}
		else if(keyMask & INPUT_MENU)
		{
			// Drop out so the menu can be displayed
			return OUTCOME_MENU;
		}
		else if(keyMask & INPUT_TOGGLE)
		{
			// Handle the toggle-show-attackers-defenders-board state chages
			human_ProcessToggle(keyMask & INPUT_TOGGLE, side, gTile[selector]);
		}
		else if(keyMask & INPUT_BACKUP)
		{
			// If a piece was selected, deselct the piece
			if(selector)
			{
				plat_DrawSquare(gTile[selector]);
				selector = 0;
				sc_cursorY = gTile[0] / 8;
				sc_cursorX = gTile[0] & 7;
				keyMask = INPUT_MOTION;
			}
			// Otherwise bring up the menu
			else
				return OUTCOME_MENU;
		}
		else if(keyMask & INPUT_UNDOREDO)
		{
			// if there's data in the undo/redo buffers to undo or redo, then do the undo/redo
			if(((keyMask & INPUT_UNDO) && undo_CanUndo()) || ((keyMask & INPUT_REDO) && undo_CanRedo()))
			{
				char numUndo = 1;
				
				// If there's AI, undo 2 moves, to get back to the humans' previous move
				if(gUserMode != (USER_BLACK | USER_WHITE))
				   numUndo = 2;
				   
				do
				{
					plat_DrawSquare(gTile[0]);
					if(selector)
						plat_DrawSquare(gTile[1]);

					if(keyMask & INPUT_UNDO)
						undo_Undo();
					else
						undo_Redo();

					plat_DrawSquare(gTile[0]);
					plat_DrawSquare(gTile[1]);
					if(NULL_TILE != gTile[2])
					{
						plat_DrawSquare(gTile[2]);
						gTile[2] = NULL_TILE;
					}
					if(NULL_TILE != gTile[3])
					{
						plat_DrawSquare(gTile[3]);
						gTile[3] = NULL_TILE;
					}

					gCursorPos[1-side][0] = gTile[0] / 8;
					gCursorPos[1-side][1] = gTile[0] & 7;

					// Undo scrolls down so updates at the top of the log, redo scrolls up
					if(keyMask & INPUT_UNDO)
						frontend_LogMove(1);
					else
						frontend_LogMove(0);
				} while(--numUndo);
				
				// Fix the Attack DB
				board_PlacePieceAttacks();

				// If 2 humans are playing, return so sides can switch
				if(gUserMode == (USER_BLACK | USER_WHITE))
					return OUTCOME_OK;
				
				keyMask = INPUT_MOTION;
			}
			else
			{
				// if there's nothing in the undo/redo buffer, show a message to say so
				// This could be if all moves have been undone or redone also
				plat_ShowMessage((keyMask & INPUT_UNDO) ? gszNoUndo : gszNoRedo, HCOLOR_INVALID);
			}
		}
		else if(keyMask & INPUT_SELECT)
		{

			if(!selector && gColor[0] == side && gNumMoves)
			{
				// If the cursor is on a piece of this turn, that has moved and no other piece
				// has yet been selected, then select this piece
				++selector;
				keyMask = INPUT_MOTION;
			}
			else if(gTile[0] == gTile[1] && gColor[0] == side)
			{
				// If the selected tile is re-selected, deselct it
				--selector;
				keyMask = INPUT_MOTION;
			}
			else if(selector && validMove)
			{
				// If a dest is selected for the selected tile, try to do the move
				// gOutcome of 0 is OUTCOME_INVALID, example moving into check
				if((gOutcome = board_ProcessAction()))
				{
					// If no piece was taken, update the move without taking counter
					// else reset it to zero
					if(NONE != (gPiece[1] & PIECE_DATA))
						gMoveCounter = 0;
					else if(NUM_MOVES_TO_DRAW == ++gMoveCounter)
						gOutcome = OUTCOME_DRAW;

					// Add the move to the undo stack and update the display
					undo_AddMove();
					plat_DrawSquare(gTile[0]);
					plat_DrawSquare(gTile[1]);
					if(NULL_TILE != gTile[2])
					{
						plat_DrawSquare(gTile[2]);
						gTile[2] = NULL_TILE;
					}
					if(NULL_TILE != gTile[3])
					{
						plat_DrawSquare(gTile[3]);
						gTile[3] = NULL_TILE;
					}
					
					// Log the move
					frontend_LogMove(0);
					
					// This will exit the while loop
					++selector;
				}
				else
				{
					// Show a message to indicate that the move was invalid
					plat_ShowMessage(gszInvalid,HCOLOR_INVALID);			
					keyMask = INPUT_MOTION;
				}
			}
		}
		// This does nothing on the C64 but some other platforms may need a
		// screen refresh - since this function doesn't always fall back to main
		plat_UpdateScreen();
	} while(selector < 2);

	// Save the cursor positions for next time
	gCursorPos[side][0] = sc_cursorY;
	gCursorPos[side][1] = sc_cursorX;
	
	return OUTCOME_OK;
}
