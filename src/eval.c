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

// Endgame pawns: the reason a won ending gets drawn.  The middlegame table
// pays 50 for a pawn on the seventh and 5 for one at home, so marching a pawn
// the length of the board earns 45 - nine a move - while the promotion that
// justifies it is worth 800 and sits well past the horizon.  The engine had no
// gradient to climb, so pushing and shuffling scored the same and it shuffled.
//
// Rank is nearly all of it here; the file barely matters once the pieces are
// off.  127 is not a round number, it is what a signed char holds
static const signed char sc_pstPawnEnd[64] =
{
	  0,  0,  0,  0,  0,  0,  0,  0,
	127,127,127,127,127,127,127,127,
	 85, 85, 85, 85, 85, 85, 85, 85,
	 50, 50, 50, 50, 50, 50, 50, 50,
	 28, 28, 28, 28, 28, 28, 28, 28,
	 14, 14, 14, 14, 14, 14, 14, 14,
	  5,  5,  5,  5,  5,  5,  5,  5,
	  0,  0,  0,  0,  0,  0,  0,  0
};

// Endgame king: the opposite advice.  Once the queens are off, a king behind
// its pawns on the back rank is a spectator - it has to walk to the middle and
// help.  The engine drew 62 games from the opening set still a clear piece up,
// and a king that will not come out is most of why
static const signed char sc_pstKingEnd[64] =
{
	-50,-40,-30,-20,-20,-30,-40,-50,
	-30,-20,-10,  0,  0,-10,-20,-30,
	-30,-10, 20, 30, 30, 20,-10,-30,
	-30,-10, 30, 40, 40, 30,-10,-30,
	-30,-10, 30, 40, 40, 30,-10,-30,
	-30,-10, 20, 30, 30, 20,-10,-30,
	-30,-30,  0,  0,  0,  0,-30,-30,
	-50,-30,-30,-30,-30,-30,-30,-50
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
int geEvalEnd;
int gePhase;

/*-----------------------------------------------------------------------*/
// Non-pawn material on the board, both sides, in hundredths of a pawn.  6400
// at the start, 0 with bare kings.  Carried by make/unmake like the score,
// because working it out per node was what killed this term the first time -
// the cost was never the table, it was touching 32 pieces to decide which
// table to use
#define PHASE_ENDGAME		3200
int eval_PhaseDelta(const t_engMove *move, char piece, char captured)
{
	char promote = move->m_flags & ENG_MF_PROMO;
	int delta = 0;

	if(NONE != (captured & PIECE_DATA) && PAWN != (captured & PIECE_DATA))
		delta -= gcPieceValue[captured & PIECE_DATA];

	// a promoted pawn arrives as non-pawn material
	if(promote)
		delta += gcPieceValue[promote];

	(void)piece;
	return delta;
}

/*-----------------------------------------------------------------------*/
// What this piece on this square is worth in an ending *over and above* what
// the middlegame tables already counted.  Zero for everything except pawns and
// kings, which are the only two whose right square depends on the phase
static int endBonus(char piece, char sq)
{
	char kind = piece & PIECE_DATA;
	char tile = ENG_TO_TILE(sq);
	int value;

	if(PAWN == kind)
	{
		tile = (piece & PIECE_WHITE) ? tile : (tile ^ 56);
		value = sc_pstPawnEnd[tile] - sc_pstPawn[tile];
	}
	else if(KING == kind)
	{
		tile = (piece & PIECE_WHITE) ? tile : (tile ^ 56);
		value = sc_pstKingEnd[tile] - sc_pstKing[tile];
	}
	else
		return 0;

	return (piece & PIECE_WHITE) ? value : -value;
}

/*-----------------------------------------------------------------------*/
// The second running total, carried by make/unmake exactly like the first.
//
// Interpolating between two sets of tables normally means keeping a whole
// second score and blending them.  Keeping the *difference* instead is the
// same thing for less: geEvalScore stays the middlegame total it always was,
// nothing that reads it changes, and this holds only what the endgame would
// add.  It is zero for every piece but pawns and kings, so most moves cost
// nothing to track
int eval_EndDelta(const t_engMove *move, char piece, char captured)
{
	char to = move->m_to, flags = move->m_flags;
	char promote = flags & ENG_MF_PROMO;
	char white = piece & PIECE_WHITE;
	int delta;

	delta = endBonus(promote ? (promote | white) : piece, to) -
	        endBonus(piece, move->m_from);

	if(NONE != (captured & PIECE_DATA))
	{
		char victim = (flags & ENG_MF_ENPASSANT) ? (white ? to + 16 : to - 16) : to;

		delta -= endBonus(captured, victim);
	}

	// castling moves a rook, which has no endgame bonus - but say so rather
	// than leave the reader wondering whether it was forgotten
	return delta;
}

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

	// and the endgame difference, the same way
	geEvalEnd = 0;
	for(sq = 0; sq < 0x78; ++sq)
	{
		if(ENG_OFFBOARD(sq))
			continue;
		geEvalEnd += endBonus(geBoard[sq], sq);
	}

	// the phase has to be rebuilt from the board for the same reason the score
	// does - pieces got here without going through eng_Make
	gePhase = 0;
	for(sq = 0; sq < 0x78; ++sq)
	{
		char kind = geBoard[sq] & PIECE_DATA;

		if(ENG_OFFBOARD(sq) || NONE == kind || PAWN == kind)
			continue;
		gePhase += gcPieceValue[kind];
	}
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
	int score = geEvalScore;

	// Blend in the endgame tables by how much material is left: nothing in the
	// middlegame, all of it with bare kings, in four steps.  Four steps rather
	// than a true interpolation because the weight then costs shifts instead of
	// a multiply, and the middlegame leaves immediately without touching it
	if(EVAL_HAS(EVAL_ENDGAME) && gePhase < PHASE_ENDGAME)
	{
		int adj = geEvalEnd;

		switch((char)((PHASE_ENDGAME - gePhase) >> 10))
		{
			case 0:  score += adj >> 2;               break;
			case 1:  score += adj >> 1;               break;
			case 2:  score += (adj >> 1) + (adj >> 2); break;
			default: score += adj;                    break;
		}
	}

	return (side == SIDE_WHITE) ? score : -score;
}
