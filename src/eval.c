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
 *
 *	One term is not a running total and could never be: mateDrive takes both
 *	kings, so it is not a property of a piece on a square.  It is affordable
 *	anyway because of *where* it runs rather than what it costs - see the note
 *	above it.
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
// Manhattan distance from the centre, by file or by rank.  The two added give
// 0 on the middle four squares and 6 in a corner
static const signed char sc_centreDist[8] = { 3, 2, 1, 0, 0, 1, 2, 3 };

// How far ahead the winning side has to be before chasing the enemy king is
// the right idea.  A minor piece: below that the position is not won, and
// walking your king at an opponent who still has material is how you lose it
#define DRIVE_GATE			400

// And how little has to be left on the board.  **This bound is the whole
// difference between a term that works and one that lost a match**, and it was
// found by playing Sargon II rather than by any test in tests/.
//
// The first version gated only on PHASE_ENDGAME, which is 3200 - so the drive
// was live with two rooks and two minors still on, where "walk the enemy king
// to the corner" is not advice, it is nonsense.  Measured against Sargon over
// 32 games it turned 8 fifty-move draws into 0 exactly as designed, and turned
// them into *losses*: 51.6% became 31.2%.  The move it changed was in a
// position where cc65 was a piece down, not up, at gePhase 1650.
//
// Every mate this term exists for is far below that: king and rook 500, bishop
// and knight 660, queen 900, two rooks 1000.  The bound therefore sits just
// above what has actually been verified rather than at a guess about what else
// might benefit - queen against a lone minor is 1220 and is *not* covered,
// deliberately, because nothing here has measured it
#define DRIVE_PHASE			1100

/*-----------------------------------------------------------------------*/
// Two things, and they are the whole of every basic mate.  Push the losing
// king away from the centre, because a king in the middle of the board cannot
// be mated by a rook; and bring the winning king up to it, because no lone
// piece mates without its king.
//
// **The weights are not the textbook ones, and that is a measurement rather
// than an oversight.**  The literature uses about 4.7 and 1.6 - corner drive
// worth three times king proximity - and tried here that is clearly worse than
// weighting them almost equally.  Five ratios measured over the thirteen
// endings in tests/search.c, counting mates before the fifty-move rule at all
// four levels: 10:4 gave 12/12/13/13, 10:6 gave 9/13/13/13, 6:4 gave
// 10/9/13/13, and 10:8 gave 12/13/13/13 with the fastest mate at every level.
// Halving proximity to 10:2 collapsed level 1 to 7 of 13.
//
// The reason is depth.  The textbook weights assume a search that can see the
// corner drive pay off; at four plies it cannot, and a king that is not already
// close when the enemy king reaches the edge has to spend moves walking there
// while the fifty-move counter runs.  Proximity is the term that pays inside
// the horizon, so this engine wants far more of it than a deep one would.
//
// Only the losing king's square matters for the first term.  The winning king
// is already told where to go by sc_pstKingEnd, and saying it twice would have
// the two terms fighting over the same squares
static int mateDrive(char winner, char loser)
{
	char lf = ENG_FILE(loser),  lr = loser >> 4;
	char wf = ENG_FILE(winner), wr = winner >> 4;
	char corner, apart;

	corner = sc_centreDist[lf] + sc_centreDist[lr];
	apart  = ((wf > lf) ? (wf - lf) : (lf - wf)) +
	         ((wr > lr) ? (wr - lr) : (lr - wr));

	// x10 and x8 as shifts, for the same reason the blend below uses them
	return (int)((corner << 3) + (corner << 1)) + (int)((14 - apart) << 3);
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
	if(gePhase < PHASE_ENDGAME)
	{
		if(EVAL_HAS(EVAL_ENDGAME))
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

		// And the reason to finish.  This is the only term that reads the board
		// at eval time, which is the cost that killed two terms in Phase 4 - so
		// note where it sits: behind *two* gates, one on how little is left and
		// one on who is winning.  It is two array reads and a handful of shifts,
		// it cannot run anywhere but a bare-king ending, and such an ending is
		// one the search walks at a few hundred nodes rather than twenty
		// thousand.  The expensive place and the place this fires are disjoint.
		//
		// Read after the blend on purpose: the second gate is asking "am I
		// winning", and the blended score is the engine's own answer to that
		if(EVAL_MATEDRIVE_ON && EVAL_HAS(EVAL_MATEDRIVE) && gePhase <= DRIVE_PHASE)
		{
			if(score > DRIVE_GATE)
				score += mateDrive(geKing[SIDE_WHITE], geKing[SIDE_BLACK]);
			else if(score < -DRIVE_GATE)
				score -= mateDrive(geKing[SIDE_BLACK], geKing[SIDE_WHITE]);
		}
	}

	return (side == SIDE_WHITE) ? score : -score;
}
