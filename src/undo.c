/*
 *	undo.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "undo.h"
#include "board.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
// The size of the undo buffer.	 This buffer is a circular buffer and
// access is available to UNDO_STACK_SIZE-1 moves in the past
// This implementation does math with char's so 255 is the max
#define UNDO_STACK_SIZE		255

/*-----------------------------------------------------------------------*/
// m_piece1 is in 2 parts. Bits 0-3 is the piece that was at gPiece[1]
// whereas bits 4-7 is the piece that's there now - which isn't gPiece[0] if
// there was a pawn promotion.
// m_code is a bit structure: Bits are:
//	 0 - gColor[0], 1 - gColor[1], 2 - gMove[0], 3 - gMove[1], 
//   4 - en passant, 5-7 - gOutcome
typedef struct tag_UndoEntry
{
	char m_tile0;
	char m_tile1;
	char m_piece0;
	char m_piece1;
	char m_code; 
} t_UndoEntry;

/*-----------------------------------------------------------------------*/
// the undo stack and 3 pointers to use it
t_UndoEntry stu_undoStack[UNDO_STACK_SIZE];
static char sc_undoTop, sc_undoBottom, sc_undoPtr;

/*-----------------------------------------------------------------------*/
void undo_Init()
{
	sc_undoTop = sc_undoBottom = sc_undoPtr = 0;
}

/*-----------------------------------------------------------------------*/
// Store the globals for tile, piece, color, move and outcome on the undo
// stack
void undo_AddMove()
{
	stu_undoStack[sc_undoPtr].m_tile0 = gTile[0];
	stu_undoStack[sc_undoPtr].m_tile1 = gTile[1];
	stu_undoStack[sc_undoPtr].m_piece0 = gPiece[0];
	stu_undoStack[sc_undoPtr].m_piece1 = gPiece[1];
	// Back up the piece found on the board at position gTile1.	 If a pawn was promoted
	// it's not the same piece that left gTile[0]
	stu_undoStack[sc_undoPtr].m_piece1 |= (gpChessBoard[gTile[1]] & PIECE_DATA) << 4;
	// Pack the status items into a single char
	stu_undoStack[sc_undoPtr].m_code = gColor[0] | (gColor[1] << 1) | (gMove[0] >> 4) | (gMove[1] >> 3) | (gOutcome << 5);
	
	// if there was an en passant capture, record it
	if(gTile[2] != NULL_TILE && gTile[3] == NULL_TILE)
		stu_undoStack[sc_undoPtr].m_code |= PAWN_ENPASSANT;

	if(++sc_undoPtr == UNDO_STACK_SIZE)
		sc_undoPtr = 0;
		
	if(sc_undoPtr == sc_undoBottom)
	{
		if(++sc_undoBottom == UNDO_STACK_SIZE)
			sc_undoBottom = 0;
	}
	
	sc_undoTop = sc_undoPtr;
}

/*-----------------------------------------------------------------------*/
// This routine doesn't check if an undo is available. canUndo must be 
// checked before calling this
void undo_Undo()
{
	if(sc_undoPtr == 0)
		sc_undoPtr = UNDO_STACK_SIZE;
		
	--sc_undoPtr;
	
	// In the case of an undo the move might have turned off the en passant
	// opportunuti created by the previous move, so go look at that move
	// to see if it created an en passant opportunity and if it did, reset
	// gEPPawn for the opportunuty
	gEPPawn = NULL_TILE;
	if(undo_FindUndoLine(0))
	{
		if(!gMove[0] && PAWN == gPiece[0])
			board_ProcessEnPassant(ENPASSANT_MAYBE);
	}

	gTile[0] = stu_undoStack[sc_undoPtr].m_tile0;
	gTile[1] = stu_undoStack[sc_undoPtr].m_tile1;
	gPiece[0] = stu_undoStack[sc_undoPtr].m_piece0;
	// For undo, restore the piece that was at gTile1 before the move
	gPiece[1] = stu_undoStack[sc_undoPtr].m_piece1 & PIECE_DATA;
	gColor[0] = stu_undoStack[sc_undoPtr].m_code & 1;
	gColor[1] = (stu_undoStack[sc_undoPtr].m_code & 2) >> 1;
	gMove[0] = (stu_undoStack[sc_undoPtr].m_code & 4) << 4;
	gMove[1] = (stu_undoStack[sc_undoPtr].m_code & 8) << 3;
	gOutcome = (stu_undoStack[sc_undoPtr].m_code >> 5) & OUTCOME_MASK;

	gpChessBoard[gTile[0]] = gPiece[0] | (gColor[0] << 7) | gMove[0];
	gpChessBoard[gTile[1]] = gPiece[1] | (gColor[1] << 7) | gMove[1];

	// if en passant set, also restore the en passant taken pawn
	if(stu_undoStack[sc_undoPtr].m_code & PAWN_ENPASSANT)
	{
		board_ProcessEnPassant(ENPASSANT_UNTAKE);
	}
	else if(KING == gPiece[0])
	{
		gKingData[gColor[0]] = gTile[0];
		board_ProcessCastling(2, 3);
	}
}

/*-----------------------------------------------------------------------*/
void undo_Redo()
{
	gTile[0] = stu_undoStack[sc_undoPtr].m_tile0;
	gTile[1] = stu_undoStack[sc_undoPtr].m_tile1;
	// Put the piece on gTile1 that was there after the move
	gPiece[0] = (stu_undoStack[sc_undoPtr].m_piece1 >> 4) & PIECE_DATA;
	gPiece[1] = stu_undoStack[sc_undoPtr].m_piece1 & 0x0f;
	gColor[0] = stu_undoStack[sc_undoPtr].m_code & 1;
	gColor[1] = (stu_undoStack[sc_undoPtr].m_code & 2) >> 1;
	gMove[0] = (stu_undoStack[sc_undoPtr].m_code & 4) << 4;
	gMove[1] = (stu_undoStack[sc_undoPtr].m_code & 8) << 3;
	gOutcome = (stu_undoStack[sc_undoPtr].m_code >> 5) & OUTCOME_MASK;
	
	if(stu_undoStack[sc_undoPtr].m_code & PAWN_ENPASSANT)
	{
		board_ProcessEnPassant(ENPASSANT_TAKE);
	}
	else if(KING == gPiece[0])
	{
		gKingData[gColor[0]] = gTile[1];
		board_ProcessCastling(3, 2);
	}
	else if(PAWN == gPiece[0])
	{
		board_ProcessEnPassant(ENPASSANT_MAYBE);
	}

	gpChessBoard[gTile[0]] = NONE;

	// Flag the piece now on gTile1 as having moved, not with the
	// move status it had before the move or the status the piece
	// on gTile1 had previously
	gpChessBoard[gTile[1]] = gPiece[0] | (gColor[0] << 7) | PIECE_MOVED;

	if(++sc_undoPtr == UNDO_STACK_SIZE)
		sc_undoPtr = 0;
}

/*-----------------------------------------------------------------------*/
// Look backwards in the undo stack to see what the move was, linesBack
// ago.	 Beware: This function also changes the global gTile, etc. 
// variables even though it's just looking back.
char undo_FindUndoLine(char linesBack)
{
	char line;
	
	// Since the undoPtr is always pointing 1 ahead of the last move,
	// asking for linesBack 0 is asing for 1 before the current undoPtr
	// Can't ask further back than the # of entries being kept
	if(++linesBack >= UNDO_STACK_SIZE)
		return 0;

	// check for a wrap in the circular buffer
	if(sc_undoPtr < linesBack)
	{
		// Can't give back entries further back than have been added
		if(sc_undoPtr >= sc_undoBottom)
			return 0;
		
		line = UNDO_STACK_SIZE - (linesBack - sc_undoPtr);
	}
	else
	{
		line = sc_undoPtr - linesBack;
		// Catches the case where the bottom > 0 due to wrap
		// and the undoPtr is > bottom due to previous undo's
		// and linesBack is durther back than what's available
		if(sc_undoBottom <= sc_undoPtr && line < sc_undoBottom)
			return 0;
	}

	gTile[0] = stu_undoStack[line].m_tile0;
	gTile[1] = stu_undoStack[line].m_tile1;
	gPiece[0] = stu_undoStack[line].m_piece0;
	gPiece[1] = stu_undoStack[line].m_piece1 & 0x0f;
	gColor[0] = stu_undoStack[line].m_code & 1;
	gColor[1] = (stu_undoStack[line].m_code & 2) >> 1;
	gMove[0] = (stu_undoStack[line].m_code & 4) << 4;
	gMove[1] = (stu_undoStack[line].m_code & 8) << 3;
	gOutcome = (stu_undoStack[line].m_code >> 5) & OUTCOME_MASK;
	
	return 1;
}

/*-----------------------------------------------------------------------*/
char undo_CanUndo()
{
	return sc_undoPtr != sc_undoBottom;
}

/*-----------------------------------------------------------------------*/
char undo_CanRedo()
{
	return sc_undoPtr != sc_undoTop;
}
