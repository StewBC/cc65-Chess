/*
 *	eval.c
 *	cc65 Chess
 *
 *	Material plus piece-square tables.  Deliberately plain: the search is what
 *	makes the engine play, and a simple evaluation that is *correct* is worth
 *	far more than a clever one that is not.  Phase 4 is where terms get added,
 *	one at a time, each kept only if self-play says it wins.
 *
 *	The tables are written the way a board is printed, index 0 = a8 through
 *	index 63 = h1, from white's point of view.  Black reads the same table with
 *	the rank flipped, which is (tile ^ 56).
 *
 *	Phase 5 turned the score into a running total.  eval_Position used to walk
 *	the board at every node, which is why Phase 4 could measure two good terms
 *	and still have to throw them away: at 1 MHz, anything done per piece per
 *	node is paid for twenty thousand times a move.  Now the score moves with
 *	the pieces - a handful of table lookups per move made - and asking for it
 *	is a read.
 */

#include "types.h"
#include "engine.h"
#include "eval.h"

/*-----------------------------------------------------------------------*/
// Hundredths of a pawn.  The king has no material value - losing it is not a
// score, it is the end of the search
const int gcPieceValue[PAWN+1] =
{
	0,		// NONE
	500,	// ROOK
	320,	// KNIGHT
	330,	// BISHOP
	900,	// QUEEN
	0,		// KING
	100,	// PAWN
};

/*-----------------------------------------------------------------------*/
static const signed char sc_pstPawn[64] =
{
	  0,  0,  0,  0,  0,  0,  0,  0,
	 50, 50, 50, 50, 50, 50, 50, 50,
	 10, 10, 20, 30, 30, 20, 10, 10,
	  5,  5, 10, 25, 25, 10,  5,  5,
	  0,  0,  0, 20, 20,  0,  0,  0,
	  5, -5,-10,  0,  0,-10, -5,  5,
	  5, 10, 10,-20,-20, 10, 10,  5,
	  0,  0,  0,  0,  0,  0,  0,  0
};

static const signed char sc_pstKnight[64] =
{
	-50,-40,-30,-30,-30,-30,-40,-50,
	-40,-20,  0,  0,  0,  0,-20,-40,
	-30,  0, 10, 15, 15, 10,  0,-30,
	-30,  5, 15, 20, 20, 15,  5,-30,
	-30,  0, 15, 20, 20, 15,  0,-30,
	-30,  5, 10, 15, 15, 10,  5,-30,
	-40,-20,  0,  5,  5,  0,-20,-40,
	-50,-40,-30,-30,-30,-30,-40,-50
};

static const signed char sc_pstBishop[64] =
{
	-20,-10,-10,-10,-10,-10,-10,-20,
	-10,  0,  0,  0,  0,  0,  0,-10,
	-10,  0,  5, 10, 10,  5,  0,-10,
	-10,  5,  5, 10, 10,  5,  5,-10,
	-10,  0, 10, 10, 10, 10,  0,-10,
	-10, 10, 10, 10, 10, 10, 10,-10,
	-10,  5,  0,  0,  0,  0,  5,-10,
	-20,-10,-10,-10,-10,-10,-10,-20
};

static const signed char sc_pstRook[64] =
{
	  0,  0,  0,  0,  0,  0,  0,  0,
	  5, 10, 10, 10, 10, 10, 10,  5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	 -5,  0,  0,  0,  0,  0,  0, -5,
	  0,  0,  0,  5,  5,  0,  0,  0
};

static const signed char sc_pstQueen[64] =
{
	-20,-10,-10, -5, -5,-10,-10,-20,
	-10,  0,  0,  0,  0,  0,  0,-10,
	-10,  0,  5,  5,  5,  5,  0,-10,
	 -5,  0,  5,  5,  5,  5,  0, -5,
	  0,  0,  5,  5,  5,  5,  0, -5,
	-10,  5,  5,  5,  5,  5,  0,-10,
	-10,  0,  5,  0,  0,  0,  0,-10,
	-20,-10,-10, -5, -5,-10,-10,-20
};

// Middlegame king: stay home, stay behind the pawns
static const signed char sc_pstKing[64] =
{
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-30,-40,-40,-50,-50,-40,-40,-30,
	-20,-30,-30,-40,-40,-30,-30,-20,
	-10,-20,-20,-20,-20,-20,-20,-10,
	 20, 20,  0,  0,  0,  0, 20, 20,
	 20, 30, 10,  0,  0, 10, 30, 20
};

// Indexed by the piece values in types.h: NONE, ROOK, KNIGHT, BISHOP, QUEEN,
// KING, PAWN
static const signed char *sc_pst[PAWN+1] =
{
	0,
	sc_pstRook,
	sc_pstKnight,
	sc_pstBishop,
	sc_pstQueen,
	sc_pstKing,
	sc_pstPawn,
};

#ifdef EVAL_TUNING
char geEvalTerms = EVAL_ALL;
#endif

int geEvalScore;

/*-----------------------------------------------------------------------*/
// What one piece standing on one square is worth, always signed white-positive
// so the totals add without caring whose turn it is
static int pieceScore(char piece, char sq)
{
	char kind = piece & PIECE_DATA;
	char tile;

	if(NONE == kind)
		return 0;

	tile = ENG_TO_TILE(sq);

#ifdef EVAL_TUNING
	// the tuning build needs each term switchable; the shipping build never
	// pays for the tests
	{
		int value = 0;

		if(EVAL_HAS(EVAL_MATERIAL))
			value += gcPieceValue[kind];
		if(EVAL_HAS(EVAL_PST))
			value += sc_pst[kind][(piece & PIECE_WHITE) ? tile : (tile ^ 56)];

		return (piece & PIECE_WHITE) ? value : -value;
	}
#else
	// black reads the same table rank-flipped
	if(piece & PIECE_WHITE)
		return gcPieceValue[kind] + sc_pst[kind][tile];

	return -(gcPieceValue[kind] + sc_pst[kind][tile ^ 56]);
#endif
}

/*-----------------------------------------------------------------------*/
// The whole-board walk that eval_Position used to be.  It is off the hot path
// now - once per move rather than once per node - so it is written for clarity
void eval_Refresh(void)
{
	char sq;
	int score = 0;

	for(sq = 0; sq < 0x78; ++sq)
	{
		if(ENG_OFFBOARD(sq))
			continue;

		score += pieceScore(geBoard[sq], sq);
	}

	geEvalScore = score;
}

/*-----------------------------------------------------------------------*/
// See eval.h for the rule this has to obey: it may read the move and the two
// piece bytes it is handed, and nothing else.  Reading geBoard here would
// break unmake, because the board is in a different state by then
int eval_MoveDelta(const t_engMove *move, char piece, char captured)
{
	char to = move->m_to, flags = move->m_flags;
	char promote = flags & ENG_MF_PROMO;
	char white = piece & PIECE_WHITE;
	int delta;

	// the mover leaves "from" and arrives on "to", as its promoted self if it
	// promoted
	delta = pieceScore(promote ? (promote | white) : piece, to)
	      - pieceScore(piece, move->m_from);

	if(NONE != (captured & PIECE_DATA))
	{
		// an en passant victim stands beside the target square, not on it -
		// white captures upward, so the pawn it takes is a rank below
		char victim = (flags & ENG_MF_ENPASSANT) ? (white ? to + 16 : to - 16) : to;

		delta -= pieceScore(captured, victim);
	}

	// castling moves a rook as well, and the rook has a table too
	if(flags & ENG_MF_CASTLE)
	{
		char rook = ROOK | white;

		if(flags & ENG_MF_CASTLE_K)
			delta += pieceScore(rook, to - 1) - pieceScore(rook, to + 1);
		else
			delta += pieceScore(rook, to + 1) - pieceScore(rook, to - 2);
	}

	return delta;
}

/*-----------------------------------------------------------------------*/
int eval_Position(char side)
{
	// the running total is white-positive; hand it back the way round the
	// caller asked for
	return (side == SIDE_WHITE) ? geEvalScore : -geEvalScore;
}
