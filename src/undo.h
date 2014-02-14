/*
 *	undo.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _UNDO_H_
#define _UNDO_H_

void undo_Init();
void undo_AddMove();
void undo_Undo();
void undo_Redo();
char undo_FindUndoLine(char linesBack);
char undo_CanUndo();
char undo_CanRedo();

#endif //_UNDO_H_