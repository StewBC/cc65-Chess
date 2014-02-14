/*
 *	types.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#define SET_BIT(x)			(1<<(x))
#define MAX_SIZE(x,y)		((x)>=(y) ? (x) : (y))
#define MIN_SIZE(x,y)		((x)<=(y) ? (x) : (y))
#define MK_POS(y,x)			((y)*8+x)

#define SIDE_BLACK			0
#define SIDE_WHITE			1
#define USER_BLACK			1
#define USER_WHITE			2
#define NUM_PIECES_SIDE		16
#define MAX_PIECE_MOVES		28
#define NULL_TILE			128
#define NUM_MOVES_TO_DRAW	50

#define HCOLOR_WHITE		1
#define HCOLOR_BLACK		2
#define HCOLOR_EMPTY		4
#define HCOLOR_VALID		5
#define HCOLOR_INVALID		2
#define HCOLOR_SELECTED		6
#define HCOLOR_ATTACK		3

#define ATTACK_WIDTH		(2+2*NUM_PIECES_SIDE)
#define ATTACK_WHITE_OFFSET 17

#define PIECE_WHITE			SET_BIT(7)
#define PIECE_MOVED			SET_BIT(6)
#define PIECE_EXTRA_DATA	(PIECE_WHITE | PIECE_MOVED)
#define PIECE_DATA			(SET_BIT(0) | SET_BIT(1) | SET_BIT(2))

#define INPUT_UP			SET_BIT(0)
#define INPUT_RIGHT			SET_BIT(1)
#define INPUT_DOWN			SET_BIT(2)
#define INPUT_LEFT			SET_BIT(3)
#define INPUT_BACKUP		SET_BIT(4)
#define INPUT_TOGGLE_A		SET_BIT(5)
#define INPUT_TOGGLE_B		SET_BIT(6)
#define INPUT_TOGGLE_D		SET_BIT(7)
#define INPUT_SELECT		SET_BIT(8)
#define INPUT_MENU			SET_BIT(9)
#define INPUT_UNDO			SET_BIT(10)
#define INPUT_REDO			SET_BIT(11)
#define INPUT_UNDOREDO		(INPUT_UNDO | INPUT_REDO)
#define INPUT_MOTION		(INPUT_UP | INPUT_RIGHT | INPUT_DOWN | INPUT_LEFT)
#define INPUT_TOGGLE		(INPUT_TOGGLE_A | INPUT_TOGGLE_B | INPUT_TOGGLE_D)

#define PAWN_PROMOTE		SET_BIT(1)
#define PAWN_ENPASSANT		SET_BIT(4)

#define ENPASSANT_TAKE		0
#define ENPASSANT_UNTAKE	1
#define ENPASSANT_MAYBE		2

#define OUTCOME_MASK		0x07

enum
{
	NONE,
	ROOK,
	KNIGHT,
	BISHOP,
	QUEEN,
	KING,
	PAWN,
};

enum
{
	OUTCOME_INVALID,
	OUTCOME_OK,
	OUTCOME_CHECK,
	OUTCOME_CHECKMATE,
	OUTCOME_DRAW,
	OUTCOME_STALEMATE,
	OUTCOME_MENU,
	OUTCOME_ABANDON,
	OUTCOME_QUIT,
};

#endif //_TYPES_H_