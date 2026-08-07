/*
 *	engine.c
 *	cc65 Chess
 *
 *	See engine.h for the shape of things.  The two ideas that matter:
 *
 *	a) 0x88.  A square is off the board when (sq & 0x88) is set, so every edge
 *	   test is one AND.  That replaces the x>7||y>7 unsigned wraparound checks
 *	   the old generator relied on.
 *
 *	b) Attacks are asked for, not stored.  eng_IsAttacked walks outwards from
 *	   the square in question, which costs about forty board reads, instead of
 *	   regenerating a 64-square attack database.  Legality testing goes from
 *	   the most expensive thing the program does to one of the cheapest.
 */

#include <string.h>
#include "types.h"
#include "engine.h"
#include "eval.h"

/*-----------------------------------------------------------------------*/
char geBoard[128];
char geCastle;
char geEP;
char geHalfmove;
char geKing[2];

/*-----------------------------------------------------------------------*/
// Row 0 is rank 8, so white (which starts on rows 6 and 7) moves by -16
#define WHITE_PUSH		(-16)
#define BLACK_PUSH		(16)
#define PUSH(side)		((side) == SIDE_WHITE ? WHITE_PUSH : BLACK_PUSH)

// Rows a pawn starts on and promotes on, per side
#define HOME_ROW(side)		((side) == SIDE_WHITE ? 6 : 1)
#define PROMOTE_ROW(side)	((side) == SIDE_WHITE ? 0 : 7)

#define COLOR_OF(piece)		(((piece) & PIECE_WHITE) >> 7)
#define IS_EMPTY(sq)		(NONE == (geBoard[sq] & PIECE_DATA))

static const signed char sc_orthogonal[4] = { -16, 16, -1, 1 };
static const signed char sc_diagonal[4]   = { -17, -15, 15, 17 };
static const signed char sc_knight[8]     = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const signed char sc_king[8]       = { -17, -16, -15, -1, 1, 15, 16, 17 };

// How many moves the current caller has room for
static char sc_maxMoves;

// Set for the duration of one eng_GenCaptures call.  The generators are shared
// rather than duplicated: a captures-only pass walks exactly the same squares
// in exactly the same order and only declines to emit the quiet moves, so what
// comes out is a true subsequence of the full list.  That is what lets
// quiescence switch generators without changing a single move it searches -
// and it is worth more than the handful of bytes a second generator would cost
static char sc_capturesOnly;

// Squares that matter for castling rights, per side
static const char sc_kingHome[2] = { 0x04, 0x74 };	// e8, e1
static const char sc_rookK[2]    = { 0x07, 0x77 };	// h8, h1
static const char sc_rookQ[2]    = { 0x00, 0x70 };	// a8, a1

/*-----------------------------------------------------------------------*/
void eng_Clear(void)
{
	memset(geBoard, NONE, sizeof(geBoard));
	geCastle = 0;
	geEP = ENG_NO_SQUARE;
	geHalfmove = 0;
	geKing[0] = geKing[1] = ENG_NO_SQUARE;
	geEvalScore = 0;
}

/*-----------------------------------------------------------------------*/
void eng_SetStartPosition(void)
{
	static const char backRank[8] =
		{ ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
	char file;

	eng_Clear();

	for(file = 0; file < 8; ++file)
	{
		geBoard[0x00 + file] = backRank[file];					// rank 8, black
		geBoard[0x10 + file] = PAWN;							// rank 7
		geBoard[0x60 + file] = PAWN | PIECE_WHITE;				// rank 2
		geBoard[0x70 + file] = backRank[file] | PIECE_WHITE;	// rank 1, white
	}

	geKing[SIDE_BLACK] = sc_kingHome[SIDE_BLACK];
	geKing[SIDE_WHITE] = sc_kingHome[SIDE_WHITE];
	geCastle = ENG_CASTLE_ALL;

	// pieces went down without going through eng_Make, so the running score
	// has to be rebuilt rather than adjusted
	eval_Refresh();
}

/*-----------------------------------------------------------------------*/
// Walks out from "square" looking for anything of bySide that hits it.  Note
// the pawn test runs backwards: a white pawn on p attacks p-17 and p-15, so a
// white pawn attacking "square" has to be sitting on square+17 or square+15
char eng_IsAttacked(char square, char bySide)
{
	char i, sq, piece;
	signed char step, back = (bySide == SIDE_WHITE) ? -WHITE_PUSH : -BLACK_PUSH;

	// pawns
	sq = square + back - 1;
	if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == PAWN && COLOR_OF(geBoard[sq]) == bySide)
		return 1;
	sq = square + back + 1;
	if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == PAWN && COLOR_OF(geBoard[sq]) == bySide)
		return 1;

	// knights
	for(i = 0; i < 8; ++i)
	{
		sq = square + sc_knight[i];
		if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == KNIGHT && COLOR_OF(geBoard[sq]) == bySide)
			return 1;
	}

	// king
	for(i = 0; i < 8; ++i)
	{
		sq = square + sc_king[i];
		if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == KING && COLOR_OF(geBoard[sq]) == bySide)
			return 1;
	}

	// rook and queen along the ranks and files
	for(i = 0; i < 4; ++i)
	{
		step = sc_orthogonal[i];
		for(sq = square + step; !ENG_OFFBOARD(sq); sq += step)
		{
			piece = geBoard[sq] & PIECE_DATA;
			if(NONE == piece)
				continue;
			if((ROOK == piece || QUEEN == piece) && COLOR_OF(geBoard[sq]) == bySide)
				return 1;
			break;
		}
	}

	// bishop and queen along the diagonals
	for(i = 0; i < 4; ++i)
	{
		step = sc_diagonal[i];
		for(sq = square + step; !ENG_OFFBOARD(sq); sq += step)
		{
			piece = geBoard[sq] & PIECE_DATA;
			if(NONE == piece)
				continue;
			if((BISHOP == piece || QUEEN == piece) && COLOR_OF(geBoard[sq]) == bySide)
				return 1;
			break;
		}
	}

	return 0;
}

/*-----------------------------------------------------------------------*/
// Same walk, but collecting instead of stopping at the first hit
char eng_AttackersOf(char square, char bySide, char *list)
{
	char i, sq, piece, count = 0;
	signed char step, back = (bySide == SIDE_WHITE) ? -WHITE_PUSH : -BLACK_PUSH;

	sq = square + back - 1;
	if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == PAWN && COLOR_OF(geBoard[sq]) == bySide)
		list[count++] = sq;
	sq = square + back + 1;
	if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == PAWN && COLOR_OF(geBoard[sq]) == bySide)
		list[count++] = sq;

	for(i = 0; i < 8; ++i)
	{
		sq = square + sc_knight[i];
		if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == KNIGHT && COLOR_OF(geBoard[sq]) == bySide)
			list[count++] = sq;
	}

	for(i = 0; i < 8; ++i)
	{
		sq = square + sc_king[i];
		if(!ENG_OFFBOARD(sq) && (geBoard[sq] & PIECE_DATA) == KING && COLOR_OF(geBoard[sq]) == bySide)
			list[count++] = sq;
	}

	for(i = 0; i < 4; ++i)
	{
		step = sc_orthogonal[i];
		for(sq = square + step; !ENG_OFFBOARD(sq); sq += step)
		{
			piece = geBoard[sq] & PIECE_DATA;
			if(NONE == piece)
				continue;
			if((ROOK == piece || QUEEN == piece) && COLOR_OF(geBoard[sq]) == bySide)
				list[count++] = sq;
			break;
		}
	}

	for(i = 0; i < 4; ++i)
	{
		step = sc_diagonal[i];
		for(sq = square + step; !ENG_OFFBOARD(sq); sq += step)
		{
			piece = geBoard[sq] & PIECE_DATA;
			if(NONE == piece)
				continue;
			if((BISHOP == piece || QUEEN == piece) && COLOR_OF(geBoard[sq]) == bySide)
				list[count++] = sq;
			break;
		}
	}

	return count;
}

/*-----------------------------------------------------------------------*/
char eng_InCheck(char side)
{
	return eng_IsAttacked(geKing[side], 1 - side);
}

/*-----------------------------------------------------------------------*/
static char addMove(t_engMove *moves, char count, char from, char to, char flags)
{
	// sc_maxMoves is the caller's capacity.  The generator must never write
	// past it - an overrun here lands in whatever static follows the move
	// arena, which is a very confusing bug to chase down
	if(count >= sc_maxMoves)
		return count;

	moves[count].m_from = from;
	moves[count].m_to = to;
	moves[count].m_flags = flags;
	moves[count].m_score = 0;
	return count + 1;
}

/*-----------------------------------------------------------------------*/
// A pawn arriving on the back rank is four moves, not one.  The old generator
// produced a single move and let board_ProcessAction pick the piece, which is
// why its perft came up short by exactly 3 per promoting move
static char addPawnMove(t_engMove *moves, char count, char from, char to, char flags, char side)
{
	if(ENG_ROW(to) == PROMOTE_ROW(side))
	{
		count = addMove(moves, count, from, to, flags | QUEEN);
		count = addMove(moves, count, from, to, flags | ROOK);
		count = addMove(moves, count, from, to, flags | BISHOP);
		count = addMove(moves, count, from, to, flags | KNIGHT);
		return count;
	}
	return addMove(moves, count, from, to, flags);
}

/*-----------------------------------------------------------------------*/
static char genPawn(char from, char side, t_engMove *moves, char count)
{
	signed char push = PUSH(side);
	char to = from + push;
	char i;

	// one forward, and two from the home row if both squares are clear
	if(!ENG_OFFBOARD(to) && IS_EMPTY(to))
	{
		if(!sc_capturesOnly)
		{
			count = addPawnMove(moves, count, from, to, 0, side);

			if(ENG_ROW(from) == HOME_ROW(side))
			{
				char two = to + push;
				if(!ENG_OFFBOARD(two) && IS_EMPTY(two))
					count = addMove(moves, count, from, two, ENG_MF_DOUBLEPUSH);
			}
		}
		// a push onto the back rank is quiet but it is not *quiet* - quiescence
		// has always searched promotions, so the captures-only pass keeps them
		else if(ENG_ROW(to) == PROMOTE_ROW(side))
			count = addPawnMove(moves, count, from, to, 0, side);
	}

	// the two captures, including en passant
	for(i = 0; i < 2; ++i)
	{
		to = from + push + (i ? 1 : -1);

		if(ENG_OFFBOARD(to))
			continue;

		if(!IS_EMPTY(to))
		{
			if(COLOR_OF(geBoard[to]) != side)
				count = addPawnMove(moves, count, from, to, 0, side);
		}
		else if(to == geEP)
			count = addMove(moves, count, from, to, ENG_MF_ENPASSANT);
	}

	return count;
}

/*-----------------------------------------------------------------------*/
static char genStepper(char from, char side, const signed char *steps,
                       t_engMove *moves, char count)
{
	char i, to;

	for(i = 0; i < 8; ++i)
	{
		to = from + steps[i];
		if(ENG_OFFBOARD(to))
			continue;
		if(IS_EMPTY(to))
		{
			if(!sc_capturesOnly)
				count = addMove(moves, count, from, to, 0);
		}
		else if(COLOR_OF(geBoard[to]) != side)
			count = addMove(moves, count, from, to, 0);
	}
	return count;
}

/*-----------------------------------------------------------------------*/
static char genSlider(char from, char side, const signed char *steps, char numSteps,
                      t_engMove *moves, char count)
{
	char i, to;

	for(i = 0; i < numSteps; ++i)
	{
		for(to = from + steps[i]; !ENG_OFFBOARD(to); to += steps[i])
		{
			if(IS_EMPTY(to))
			{
				// captures-only still has to walk the ray to find the blocker,
				// it just does not write the empty squares down on the way
				if(!sc_capturesOnly)
					count = addMove(moves, count, from, to, 0);
				continue;
			}
			if(COLOR_OF(geBoard[to]) != side)
				count = addMove(moves, count, from, to, 0);
			break;
		}
	}
	return count;
}

/*-----------------------------------------------------------------------*/
// Castling is generated only when it is legal all the way through: rights
// still held, the squares between empty, the king not in check, and the
// square it crosses not attacked.  The square it lands on is covered by the
// ordinary king-in-check filter afterwards
static char genCastle(char side, t_engMove *moves, char count)
{
	char king = sc_kingHome[side];
	char other = 1 - side;
	char rightK = (side == SIDE_WHITE) ? ENG_CASTLE_WK : ENG_CASTLE_BK;
	char rightQ = (side == SIDE_WHITE) ? ENG_CASTLE_WQ : ENG_CASTLE_BQ;

	if(!(geCastle & (rightK | rightQ)))
		return count;

	// a king in check may not castle at all
	if(eng_IsAttacked(king, other))
		return count;

	if((geCastle & rightK) && IS_EMPTY(king+1) && IS_EMPTY(king+2) &&
	   !eng_IsAttacked(king+1, other))
		count = addMove(moves, count, king, king+2, ENG_MF_CASTLE_K);

	if((geCastle & rightQ) && IS_EMPTY(king-1) && IS_EMPTY(king-2) && IS_EMPTY(king-3) &&
	   !eng_IsAttacked(king-1, other))
		count = addMove(moves, count, king, king-2, ENG_MF_CASTLE_Q);

	return count;
}

/*-----------------------------------------------------------------------*/
// Everything one piece can do, without touching sc_capturesOnly - the public
// entry points below own that, so there is exactly one place per generation
// that decides which kind it is
static char genFrom(char sq, char side, t_engMove *moves, char maxMoves)
{
	char piece = geBoard[sq];
	char count = 0;

	sc_maxMoves = maxMoves;

	if(NONE == (piece & PIECE_DATA) || COLOR_OF(piece) != side)
		return 0;

	switch(piece & PIECE_DATA)
	{
		case PAWN:   count = genPawn(sq, side, moves, count); break;
		case KNIGHT: count = genStepper(sq, side, sc_knight, moves, count); break;
		case ROOK:   count = genSlider(sq, side, sc_orthogonal, 4, moves, count); break;
		case BISHOP: count = genSlider(sq, side, sc_diagonal, 4, moves, count); break;
		case QUEEN:
			count = genSlider(sq, side, sc_orthogonal, 4, moves, count);
			count = genSlider(sq, side, sc_diagonal, 4, moves, count);
		break;
		case KING:
			count = genStepper(sq, side, sc_king, moves, count);
			// castling belongs to the king, and only from its home square -
			// and it is never a capture, so quiescence never wants it
			if(sq == sc_kingHome[side] && !sc_capturesOnly)
				count = genCastle(side, moves, count);
		break;
	}

	return count;
}

/*-----------------------------------------------------------------------*/
static char genBoard(char side, t_engMove *moves, char maxMoves)
{
	char sq, count = 0;

	for(sq = 0; sq < 0x78; ++sq)
	{
		char piece;

		if(ENG_OFFBOARD(sq))
			continue;

		// The same test happens inside genFrom, but doing it here as well
		// turns 64 calls a move into about 16.  On a 6502 a call with four
		// arguments is not cheap
		piece = geBoard[sq];
		if(NONE == (piece & PIECE_DATA) || COLOR_OF(piece) != side)
			continue;

		count += genFrom(sq, side, &moves[count], maxMoves - count);
	}

	return count;
}

/*-----------------------------------------------------------------------*/
char eng_GenMovesFrom(char sq, char side, t_engMove *moves, char maxMoves)
{
	sc_capturesOnly = 0;
	return genFrom(sq, side, moves, maxMoves);
}

/*-----------------------------------------------------------------------*/
char eng_GenMoves(char side, t_engMove *moves, char maxMoves)
{
	sc_capturesOnly = 0;
	return genBoard(side, moves, maxMoves);
}

/*-----------------------------------------------------------------------*/
char eng_GenCaptures(char side, t_engMove *moves, char maxMoves)
{
	char count;

	sc_capturesOnly = 1;
	count = genBoard(side, moves, maxMoves);
	sc_capturesOnly = 0;

	return count;
}

/*-----------------------------------------------------------------------*/
char eng_GenLegalMoves(char side, t_engMove *moves)
{
	t_engUndo undo;
	char count = eng_GenMoves(side, moves, ENG_MAX_MOVES);
	char i = 0, out = 0;

	while(i < count)
	{
		eng_Make(&moves[i], &undo);
		if(!eng_IsAttacked(geKing[side], 1 - side))
			moves[out++] = moves[i];
		eng_Unmake(&moves[i], &undo);
		++i;
	}

	return out;
}

/*-----------------------------------------------------------------------*/
// Losing a right when a rook leaves, or is taken on, its home square
static void revokeRights(char square)
{
	if(square == sc_rookK[SIDE_WHITE])      geCastle &= ~ENG_CASTLE_WK;
	else if(square == sc_rookQ[SIDE_WHITE]) geCastle &= ~ENG_CASTLE_WQ;
	else if(square == sc_rookK[SIDE_BLACK]) geCastle &= ~ENG_CASTLE_BK;
	else if(square == sc_rookQ[SIDE_BLACK]) geCastle &= ~ENG_CASTLE_BQ;
}

/*-----------------------------------------------------------------------*/
void eng_Make(const t_engMove *move, t_engUndo *undo)
{
	char from = move->m_from, to = move->m_to, flags = move->m_flags;
	char piece = geBoard[from];
	char side = COLOR_OF(piece);
	char promote = flags & ENG_MF_PROMO;

	undo->m_ep = geEP;
	undo->m_castle = geCastle;
	undo->m_halfmove = geHalfmove;
	undo->m_captured = NONE;

	geEP = ENG_NO_SQUARE;
	++geHalfmove;

	if(flags & ENG_MF_ENPASSANT)
	{
		// the pawn taken sits beside the moving pawn, not on the target square
		char victim = (side == SIDE_WHITE) ? to - WHITE_PUSH : to - BLACK_PUSH;
		undo->m_captured = geBoard[victim];
		geBoard[victim] = NONE;
		geHalfmove = 0;
	}
	else if(NONE != (geBoard[to] & PIECE_DATA))
	{
		undo->m_captured = geBoard[to];
		revokeRights(to);
		geHalfmove = 0;
	}

	geBoard[to] = promote ? (promote | (piece & PIECE_WHITE)) : piece;
	geBoard[from] = NONE;

	if(PAWN == (piece & PIECE_DATA))
	{
		geHalfmove = 0;
		if(flags & ENG_MF_DOUBLEPUSH)
			geEP = (side == SIDE_WHITE) ? to - WHITE_PUSH : to - BLACK_PUSH;
	}
	else if(KING == (piece & PIECE_DATA))
	{
		geKing[side] = to;
		geCastle &= (side == SIDE_WHITE) ? ~(ENG_CASTLE_WK|ENG_CASTLE_WQ)
		                                 : ~(ENG_CASTLE_BK|ENG_CASTLE_BQ);

		if(flags & ENG_MF_CASTLE_K)
		{
			geBoard[to-1] = geBoard[to+1];
			geBoard[to+1] = NONE;
		}
		else if(flags & ENG_MF_CASTLE_Q)
		{
			geBoard[to+1] = geBoard[to-2];
			geBoard[to-2] = NONE;
		}
	}
	else if(ROOK == (piece & PIECE_DATA))
		revokeRights(from);

	// keep the running evaluation with the pieces.  eng_Unmake subtracts this
	// exact call, so the two cannot drift apart
	geEvalScore += eval_MoveDelta(move, piece, undo->m_captured);
}

/*-----------------------------------------------------------------------*/
void eng_Unmake(const t_engMove *move, const t_engUndo *undo)
{
	char from = move->m_from, to = move->m_to, flags = move->m_flags;
	char piece = geBoard[to];
	char side = COLOR_OF(piece);

	// a promoted piece goes back to being a pawn.  "moved" is the mover as it
	// stood before the move, which is what eval_MoveDelta was given by eng_Make
	char moved = (flags & ENG_MF_PROMO) ? (PAWN | (piece & PIECE_WHITE)) : piece;

	geEP = undo->m_ep;
	geCastle = undo->m_castle;
	geHalfmove = undo->m_halfmove;

	geBoard[from] = moved;
	geBoard[to] = NONE;

	if(flags & ENG_MF_ENPASSANT)
	{
		char victim = (side == SIDE_WHITE) ? to - WHITE_PUSH : to - BLACK_PUSH;
		geBoard[victim] = undo->m_captured;
	}
	else if(NONE != (undo->m_captured & PIECE_DATA))
		geBoard[to] = undo->m_captured;

	if(KING == (geBoard[from] & PIECE_DATA))
	{
		geKing[side] = from;

		if(flags & ENG_MF_CASTLE_K)
		{
			geBoard[to+1] = geBoard[to-1];
			geBoard[to-1] = NONE;
		}
		else if(flags & ENG_MF_CASTLE_Q)
		{
			geBoard[to-2] = geBoard[to+1];
			geBoard[to+1] = NONE;
		}
	}

	geEvalScore -= eval_MoveDelta(move, moved, undo->m_captured);
}
