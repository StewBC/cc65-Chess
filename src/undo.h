/*
 *	undo.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _UNDO_H_
#define _UNDO_H_

void undo_Init(void);
void undo_AddMove(void);
void undo_Undo(void);
void undo_Redo(void);
char undo_FindUndoLine(char linesBack);
char undo_CanUndo(void);
char undo_CanRedo(void);

#endif //_UNDO_H_
