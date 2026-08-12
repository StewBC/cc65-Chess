/*
 *	undo.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"
#include "engine.h"
#include "undo.h"

/*-----------------------------------------------------------------------*/
// A circular buffer, so the last UNDO_STACK_SIZE-1 moves can be taken back.
// Eight bytes an entry against the old five, but the entries are now complete
// - nothing has to be inferred on the way back out
#define UNDO_STACK_SIZE		128

/*-----------------------------------------------------------------------*/
typedef struct tag_UndoEntry
{
	char	m_from;			// 0x88 squares, as the engine uses
	char	m_to;
	char	m_flags;		// ENG_MF_* off the move
	char	m_captured;		// piece taken, NONE if none
	char	m_ep;			// en passant target before the move
	char	m_castle;		// castling rights before the move
	char	m_halfmove;		// fifty move counter before the move
	char	m_state;		// bits 0-2 outcome, bit 7 the mover's color
} t_UndoEntry;

#define STATE_OUTCOME		OUTCOME_MASK
#define STATE_WHITE			SET_BIT(7)

/*-----------------------------------------------------------------------*/
#ifdef __ATARI__
/* 1,024 bytes in the hole between the GR.8 screen and the 2K stack.
 * cold during search and never needs to be zeroed.  chessAtari.cfg
 * owns the region; this just names the array into that segment. */
#pragma bss-name (push, "UNDOBSS")
#endif
static t_UndoEntry	stu_undoStack[UNDO_STACK_SIZE];
#ifdef __ATARI__
#pragma bss-name (pop)
#endif
static char			sc_undoTop, sc_undoBottom, sc_undoPtr;

/*-----------------------------------------------------------------------*/
void undo_Init(void)
{
	sc_undoTop = sc_undoBottom = sc_undoPtr = 0;
}

/*-----------------------------------------------------------------------*/
void undo_AddMove(const t_engMove *move, const t_engUndo *undo, char side, char outcome)
{
	t_UndoEntry *entry = &stu_undoStack[sc_undoPtr];

	entry->m_from = move->m_from;
	entry->m_to = move->m_to;
	entry->m_flags = move->m_flags;
	entry->m_captured = undo->m_captured;
	entry->m_ep = undo->m_ep;
	entry->m_castle = undo->m_castle;
	entry->m_halfmove = undo->m_halfmove;
	entry->m_state = (outcome & STATE_OUTCOME) | (side == SIDE_WHITE ? STATE_WHITE : 0);

	if(++sc_undoPtr == UNDO_STACK_SIZE)
		sc_undoPtr = 0;

	// the buffer is full, so the oldest move falls off the bottom
	if(sc_undoPtr == sc_undoBottom)
		if(++sc_undoBottom == UNDO_STACK_SIZE)
			sc_undoBottom = 0;

	// anything that was ahead of here is no longer reachable
	sc_undoTop = sc_undoPtr;
}

/*-----------------------------------------------------------------------*/
// Rebuild the move and state pair the engine needs from a stack entry
static void loadEntry(const t_UndoEntry *entry, t_engMove *move, t_engUndo *undo)
{
	move->m_from = entry->m_from;
	move->m_to = entry->m_to;
	move->m_flags = entry->m_flags;
	move->m_score = 0;

	undo->m_captured = entry->m_captured;
	undo->m_ep = entry->m_ep;
	undo->m_castle = entry->m_castle;
	undo->m_halfmove = entry->m_halfmove;
}

/*-----------------------------------------------------------------------*/
// undo_CanUndo has to be true before calling this
void undo_Undo(void)
{
	t_engMove move;
	t_engUndo undo;

	if(sc_undoPtr == 0)
		sc_undoPtr = UNDO_STACK_SIZE;
	--sc_undoPtr;

	loadEntry(&stu_undoStack[sc_undoPtr], &move, &undo);
	eng_Unmake(&move, &undo);
}

/*-----------------------------------------------------------------------*/
// undo_CanRedo has to be true before calling this
void undo_Redo(void)
{
	t_engMove move;
	t_engUndo undo;

	loadEntry(&stu_undoStack[sc_undoPtr], &move, &undo);
	eng_Make(&move, &undo);

	if(++sc_undoPtr == UNDO_STACK_SIZE)
		sc_undoPtr = 0;
}

/*-----------------------------------------------------------------------*/
char undo_CanUndo(void)
{
	return sc_undoPtr != sc_undoBottom;
}

/*-----------------------------------------------------------------------*/
char undo_CanRedo(void)
{
	return sc_undoPtr != sc_undoTop;
}

/*-----------------------------------------------------------------------*/
char undo_FindUndoLine(char linesBack)
{
	const t_UndoEntry *entry;
	char line;

	// undoPtr always points one past the last move, so linesBack 0 means the
	// entry just below it
	if(++linesBack >= UNDO_STACK_SIZE)
		return 0;

	if(sc_undoPtr < linesBack)
	{
		// the request wraps around the bottom of the buffer
		if(sc_undoPtr >= sc_undoBottom)
			return 0;
		line = UNDO_STACK_SIZE - (linesBack - sc_undoPtr);
		if(line < sc_undoBottom)
			return 0;
	}
	else
	{
		line = sc_undoPtr - linesBack;
		if(sc_undoBottom <= sc_undoPtr && line < sc_undoBottom)
			return 0;
	}

	entry = &stu_undoStack[line];

	// The ports read these directly to draw a log line, in 0..63 tiles
	gTile[0] = ENG_TO_TILE(entry->m_from);
	gTile[1] = ENG_TO_TILE(entry->m_to);
	gPiece[1] = entry->m_captured & PIECE_DATA;
	// an en passant take has an empty destination but is still a capture
	if(entry->m_flags & ENG_MF_ENPASSANT)
		gPiece[1] = PAWN;
	gColor[0] = (entry->m_state & STATE_WHITE) ? SIDE_WHITE : SIDE_BLACK;
	gOutcome = entry->m_state & STATE_OUTCOME;

	return 1;
}
