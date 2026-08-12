/*
 *	engine.h
 *	cc65 Chess
 *
 *	The board core: a 0x88 board, make/unmake against a per-ply state stack,
 *	legal move generation, and attack queries by ray-cast.
 *
 *	Squares here are 0x88, 0..119, and a square is off the board when
 *	(sq & 0x88) is non-zero.  Row 0 is rank 8 and row 7 is rank 1, which
 *	matches the old gChessBoard and means white pawns move by -16.
 *
 *	The UI never sees a 0x88 square.  plat.h and everything above it keep the
 *	old 0..63 tile numbers, and ENG_TO_TILE / ENG_FROM_TILE convert at the
 *	boundary.  That is a hard rule - it is what keeps the platform files that
 *	cannot be built here from needing any changes at all.
 */

#ifndef _ENGINE_H_
#define _ENGINE_H_

#include "types.h"

/*-----------------------------------------------------------------------*/
#define ENG_OFFBOARD(sq)	((sq) & 0x88)
#define ENG_ROW(sq)			((sq) >> 4)
#define ENG_FILE(sq)		((sq) & 7)

// 0x88 square <-> the 0..63 tile the UI and the log use
#define ENG_TO_TILE(sq)		((((sq) >> 4) << 3) | ((sq) & 7))
#define ENG_FROM_TILE(t)	((((t) >> 3) << 4) | ((t) & 7))

#define ENG_NO_SQUARE		0x7F

// Castling rights, one bit each
#define ENG_CASTLE_WK		SET_BIT(0)
#define ENG_CASTLE_WQ		SET_BIT(1)
#define ENG_CASTLE_BK		SET_BIT(2)
#define ENG_CASTLE_BQ		SET_BIT(3)
#define ENG_CASTLE_ALL		(ENG_CASTLE_WK|ENG_CASTLE_WQ|ENG_CASTLE_BK|ENG_CASTLE_BQ)

// m_flags on a move
#define ENG_MF_PROMO		(SET_BIT(0)|SET_BIT(1)|SET_BIT(2))	// holds the piece
#define ENG_MF_ENPASSANT	SET_BIT(3)
#define ENG_MF_CASTLE_K		SET_BIT(4)
#define ENG_MF_CASTLE_Q		SET_BIT(5)
#define ENG_MF_DOUBLEPUSH	SET_BIT(6)
#define ENG_MF_CASTLE		(ENG_MF_CASTLE_K|ENG_MF_CASTLE_Q)

// The most moves one position can need.  The true worst case in chess is 218;
// nothing near that turns up in a real game, but the generator checks anyway
#define ENG_MAX_MOVES		128

/*-----------------------------------------------------------------------*/
// Four bytes so that indexing is a shift rather than a multiply, and so that
// move ordering in the search has somewhere to put its score
typedef struct tag_engMove
{
	char	m_from;
	char	m_to;
	char	m_flags;
	char	m_score;
} t_engMove;

/*-----------------------------------------------------------------------*/
// Everything make cannot work out for itself on the way back
typedef struct tag_engUndo
{
	char	m_captured;			// piece byte taken, NONE if none
	char	m_ep;				// en passant target before the move
	char	m_castle;			// castling rights before the move
	char	m_halfmove;			// 50 move counter before the move
} t_engUndo;

/*-----------------------------------------------------------------------*/
extern char	geBoard[128];		// the 0x88 board
extern char	geCastle;			// ENG_CASTLE_* mask
extern char	geEP;				// en passant target square, or ENG_NO_SQUARE
extern char	geHalfmove;			// plies since the last capture or pawn move
extern char	geKing[2];			// where each king is, indexed by side

/*-----------------------------------------------------------------------*/
// A running 16 bit hash of the piece placement, kept up to date by eng_Make
// and eng_Unmake exactly the way geEvalScore is - the same pattern, checked
// the same way by the fuzzer.  Castling rights and the en passant file are
// not in here; they are folded in when a position is stored or compared,
// which costs two lookups and saves keeping them incremental
extern unsigned int geHashKey;

// Rejected B1 speed candidate retained for target A/B reproduction.  Both
// forms use the same 16-bit signature and collision policy; the ring form is
// real but saved only about 0.03% of a fixed C64 middlegame replay.
#ifndef ENGINE_REPETITION_RING_KEY
#define ENGINE_REPETITION_RING_KEY 0
#endif

/*-----------------------------------------------------------------------*/
// Rebuild geHashKey from the board and start the position history again with
// the position as it now stands.  Anything that puts pieces down without
// going through eng_Make has to call this - a new game, a FEN, a test loading
// a saved position - or the history will hold another game's positions and
// repetition detection will answer about that game instead of this one
void eng_HashReset(void);

/*-----------------------------------------------------------------------*/
// geHashKey computed from the board rather than carried along, with no other
// effect.  eng_HashReset is built on it, and the fuzzer uses it to catch the
// running key drifting away from the position the way it does for the running
// evaluation - a wrong delta is silent otherwise
unsigned int eng_HashOfBoard(void);

/*-----------------------------------------------------------------------*/
// Has the position on the board been seen "needed" times before?  1 is what
// the search asks: inside a search line, one repeat already means neither
// side is making progress, and treating it as a draw is what stops the engine
// shuffling.  2 is the threefold a real game is drawn by.
//
// Only looks back as far as the fifty move counter allows, since a capture or
// a pawn move makes every position before it unreachable, and only at every
// second entry, since the others have the other side to move
char eng_IsRepetition(char needed);

#ifdef EVAL_TUNING
// Native-test view of the invariant used by ENGINE_REPETITION_RING_KEY.
char eng_HistoryMatchesPosition(void);
unsigned int eng_HistoryStateDigest(void);
#endif

/*-----------------------------------------------------------------------*/
void eng_Clear(void);
void eng_SetStartPosition(void);

/*-----------------------------------------------------------------------*/
// Is "square" attacked by anything belonging to "bySide"
char eng_IsAttacked(char square, char bySide);

/*-----------------------------------------------------------------------*/
// Every piece of "bySide" that attacks "square", written into list as 0x88
// squares.  Returns the count.  This is what the A/D/B displays are built on,
// and it is why the stored attack DB is not needed any more
char eng_AttackersOf(char square, char bySide, char *list);

/*-----------------------------------------------------------------------*/
char eng_InCheck(char side);

/*-----------------------------------------------------------------------*/
// Pseudo-legal moves for "side" - they may still leave the king in check.
// Castling is only generated when it is fully legal, since the crossed
// squares are cheaper to test here than to make and unmake
char eng_GenMoves(char side, t_engMove *moves, char maxMoves);

/*-----------------------------------------------------------------------*/
// Everything the one piece on "sq" can do, castling included when it is the
// king on its home square.  This is what the cursor needs
char eng_GenMovesFrom(char sq, char side, t_engMove *moves, char maxMoves);

/*-----------------------------------------------------------------------*/
// Captures, en passant and promotions only - what quiescence searches.  It
// used to ask eng_GenMoves for everything and throw the quiet moves away,
// which meant paying for a full generation to search maybe two moves.
//
// The guarantee this makes, and which tests/quiescence.c checks position by
// position, is that the result is exactly the subsequence eng_GenMoves would
// have produced, in the same order.  Ordering is not cosmetic here: the search
// breaks score ties by list position, so anything else would quietly change
// which move the engine plays
char eng_GenCaptures(char side, t_engMove *moves, char maxMoves);

/*-----------------------------------------------------------------------*/
// Legal moves: generated, then filtered by make/unmake
char eng_GenLegalMoves(char side, t_engMove *moves);

/*-----------------------------------------------------------------------*/
void eng_Make(const t_engMove *move, t_engUndo *undo);
void eng_Unmake(const t_engMove *move, const t_engUndo *undo);

// Quiescence disables position-history maintenance for its whole subtree.
// Nothing there reads the key or repetition ring, and setting the mode once
// at the boundary is much cheaper on cc65 than another make/unmake argument.
void eng_HistoryEnable(char enabled);

// Search can restore its four running totals from per-ply state instead of
// recomputing their deltas on unmake.  The board and history top are still
// unwound here; ordinary callers leave this disabled.
void eng_RestoreEnable(char enabled);

#endif //_ENGINE_H_
