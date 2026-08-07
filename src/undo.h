/*
 *	undo.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 *	The undo stack now records exactly what the engine needs to take a move
 *	back - the move itself plus the state make could not recompute - rather
 *	than trying to reconstruct castling and en passant from the move afterwards
 *	the way the old one did.  That reconstruction was where most of the undo
 *	bugs lived.
 */

#ifndef _UNDO_H_
#define _UNDO_H_

#include "engine.h"

/*-----------------------------------------------------------------------*/
void undo_Init(void);

// Push the move that has just been made, along with the state eng_Unmake
// needs, and the outcome for the log
void undo_AddMove(const t_engMove *move, const t_engUndo *undo, char side, char outcome);

// Take back / replay one move, board included
void undo_Undo(void);
void undo_Redo(void);

char undo_CanUndo(void);
char undo_CanRedo(void);

/*-----------------------------------------------------------------------*/
// Look back linesBack moves and load gTile, gPiece, gColor and gOutcome for
// the log display.  Returns 0 when there is no such move.
// Beware: this changes those globals as a side effect, which the platform log
// windows rely on - see plat_AddToLogWin in any of the ports
char undo_FindUndoLine(char linesBack);

#endif //_UNDO_H_
