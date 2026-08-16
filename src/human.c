/*
 *	human.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "engine.h"
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
// Track the user controlled cursor on the board
static char sc_cursorX, sc_cursorY;

/*-----------------------------------------------------------------------*/
// Handle the cursor movement
void human_ProcessInput(int keyMask)
{
	if(gGotoTile < 64)
	{
		sc_cursorX = gGotoTile & 7;
		sc_cursorY = gGotoTile / 8;
		gGotoTile = 255;
		return;
	}

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
// as well as the 'b'oard state which shows all attacks/defences
void human_ProcessToggle(int keyMask, char side, char tile)
{
	char attack = 0;

	switch(keyMask)
	{
		case INPUT_TOGGLE_B:
			gShowAttackBoard = 1 - gShowAttackBoard;
			// the counts are only kept up to date while the display is on
			board_SyncDisplay();
			plat_DrawBoard(0);
		break;

		case INPUT_TOGGLE_A:
			attack = 1;
			// Intentional fall-through since the code
			// below is the same for showing attack or defence

		case INPUT_TOGGLE_D:
		{
			char attackers[16];
			char count, i, which;

			gShowAttacks[side] ^= SET_BIT(attack);

			// 'D' wants this side's defenders, 'A' the other side's attackers
			which = attack ? (1 - side) : side;
			count = board_AttackersOf(tile, which, attackers);

			for(i = 0; i < count; ++i)
			{
				if(gShowAttacks[side] & SET_BIT(attack))
					plat_Highlight(attackers[i], 2 + attack, 0);
				else
					plat_DrawSquare(attackers[i]);
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
	char validMove = 0, selector = 0, done = 0;
	char srcTile = 0, dstTile = 0, piece, pieceColor;
	int keyMask = INPUT_MOTION;

	// get this sides' cursor
	sc_cursorY = gCursorPos[side][0];
	sc_cursorX = gCursorPos[side][1];
	gGotoTile = 255;

	do
	{
		char cursorTile = MK_POS(sc_cursorY, sc_cursorX);

		if(!selector)
			srcTile = cursorTile;
		else
			dstTile = cursorTile;

		piece = gChessBoard[sc_cursorY][sc_cursorX];
		pieceColor = (piece & PIECE_WHITE) >> 7;

		if(keyMask & INPUT_MOTION)
		{
			// If no piece selected and the cursor moved, work out where the
			// piece under it could go.  These are legal moves, not merely
			// possible ones, so a move offered can never be refused
			if(!selector)
				board_LegalMovesFrom(srcTile);
			else
			{
				validMove = board_findInList(gMoveTiles, gNumMoveTiles, dstTile);
				plat_Highlight(dstTile, validMove ? HCOLOR_ATTACK : HCOLOR_INVALID, 1);
			}

			// Show the cursor
			plat_Highlight(srcTile, selector ? HCOLOR_SELECTED :
			               NONE == (piece & PIECE_DATA) || pieceColor != side ? HCOLOR_EMPTY :
			               gNumMoveTiles ? HCOLOR_VALID : HCOLOR_INVALID, 1);
		}

		// If the cursor moved and the toggle-show-attackers/defenders states were on for this side,
		// Handle showing them for the now selected tile. Bit 2/3 says it was on, so toggle it on.
		// 2/3 is set when the selection changes, lower in this same routine
		if(gShowAttacks[side] & SET_BIT(2))
		{
			gShowAttacks[side] &= ~SET_BIT(2);
			human_ProcessToggle(INPUT_TOGGLE_D, side, cursorTile);
		}

		if(gShowAttacks[side] & SET_BIT(3))
		{
			gShowAttacks[side] &= ~SET_BIT(3);
			human_ProcessToggle(INPUT_TOGGLE_A, side, cursorTile);
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
				human_ProcessToggle(INPUT_TOGGLE_D, side, cursorTile);
			}

			if(gShowAttacks[side] & SET_BIT(1))
			{
				gShowAttacks[side] |= SET_BIT(3);
				human_ProcessToggle(INPUT_TOGGLE_A, side, cursorTile);
			}
		}

		if(keyMask & INPUT_MOTION)
		{
			// Erase the cursor and move it
			plat_DrawSquare(cursorTile);
			human_ProcessInput(keyMask & INPUT_MOTION);
		}
		else if(keyMask & INPUT_MENU)
		{
			// Drop out so the menu can be displayed
			return OUTCOME_MENU;
		}
		else if(keyMask & INPUT_TOGGLE)
		{
			// Handle the toggle-show-attackers-defenders-board state changes
			human_ProcessToggle(keyMask & INPUT_TOGGLE, side, cursorTile);
		}
		else if(keyMask & INPUT_BACKUP)
		{
			// If a piece was selected, deselect the piece
			if(selector)
			{
				plat_DrawSquare(dstTile);
				selector = 0;
				sc_cursorY = srcTile / 8;
				sc_cursorX = srcTile & 7;
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
					if(keyMask & INPUT_UNDO)
					{
						if(!undo_CanUndo())
							break;
						undo_Undo();
					}
					else
					{
						if(!undo_CanRedo())
							break;
						undo_Redo();
					}

					// Undo scrolls down so updates at the top of the log, redo scrolls up
					frontend_LogMove((keyMask & INPUT_UNDO) ? 1 : 0);

				} while(--numUndo);

				board_SyncDisplay();
				plat_DrawBoard(0);

				// put the cursor where the restored move came from
				undo_FindUndoLine(0);
				gCursorPos[1-side][0] = gTile[0] / 8;
				gCursorPos[1-side][1] = gTile[0] & 7;

				selector = 0;

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
			if(!selector && pieceColor == side && NONE != (piece & PIECE_DATA) && gNumMoveTiles)
			{
				// If the cursor is on a piece of this turn that has moves, and
				// no other piece has yet been selected, then select this piece
				++selector;
				keyMask = INPUT_MOTION;
			}
			else if(selector && srcTile == dstTile)
			{
				// If the selected tile is re-selected, deselect it
				selector = 0;
				keyMask = INPUT_MOTION;
			}
			else if(selector && validMove)
			{
				t_engMove move;
				char promote = NONE;

				// A pawn reaching the back rank needs a rank choosing first
				if(board_IsPromotion(srcTile, dstTile))
					promote = frontend_GetPromotion();

				if(board_FindMove(srcTile, dstTile, promote, &move))
				{
					gOutcome = board_ApplyMove(&move, side);

					plat_DrawSquare(srcTile);
					plat_DrawSquare(dstTile);

					// castling also moves a rook, en passant clears a third
					// tile, and a promotion changed the piece - redraw
					if(move.m_flags & (ENG_MF_CASTLE | ENG_MF_ENPASSANT))
						plat_DrawBoard(0);

					frontend_LogMove(0);
					done = 1;
				}
				else
					keyMask = INPUT_MOTION;
			}
		}
		// This does nothing on the C64 but some other platforms may need a
		// screen refresh - since this function doesn't always fall back to main
		plat_UpdateScreen();
	} while(!done);

	// Save the cursor positions for next time
	gCursorPos[side][0] = sc_cursorY;
	gCursorPos[side][1] = sc_cursorX;

	return gOutcome;
}
