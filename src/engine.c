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
unsigned int geHashKey;

/*-----------------------------------------------------------------------*/
// Position history, for repetition detection.  A ring rather than a stack
// because it is pushed by eng_Make and popped by eng_Unmake, which means it
// holds the moves of the real game and the moves of the search line above it
// in one array, with no second mechanism and nothing for a caller to remember
// to call.  128 entries is more than the fifty move rule can ever need, so
// the wrap can never eat an entry that was still reachable.
//
// sc_hashValid is not decoration.  A position loaded from a FEN can arrive
// with a fifty move counter of 30 and no history at all behind it, and
// without a count of what has actually been pushed the scan would walk 30
// entries into whatever the last game left in the ring
#define HASH_RING		128
#define HASH_MASK		(HASH_RING - 1)

static unsigned int	su_hashRing[HASH_RING];
static char			sc_hashTop;			// next slot to write, wraps
static char			sc_hashValid;		// entries that can be trusted

static const unsigned int sc_zobrist[12 * 64] =
{
	// black rook
	0xA3F4, 0x8A14, 0x08A6, 0xCC25, 0x5818, 0xC8A4, 0xAD08, 0x9EBF,
	0x2287, 0x06F1, 0xFFF9, 0x3E2F, 0xBCA8, 0xF804, 0xEED3, 0x7F42,
	0x34AA, 0x9D08, 0x088A, 0x76FE, 0xD39D, 0x2293, 0xE824, 0xCE0F,
	0xC6F2, 0x97E4, 0xDC06, 0x15CD, 0xD136, 0xE92D, 0x7F00, 0x5BF0,
	0x0E67, 0xAE51, 0xE382, 0x69C1, 0xBC88, 0x74F0, 0x6106, 0xBBEF,
	0xA458, 0x4AEC, 0x2044, 0xE45B, 0x6FF3, 0x9009, 0xA121, 0xB713,
	0x1AA1, 0x1D90, 0x1BDB, 0x3CA8, 0xD69B, 0x8B90, 0xE606, 0xA10F,
	0x7005, 0x0478, 0x3CAC, 0x4731, 0xAA67, 0x71B4, 0xE1D6, 0x56A0,
	// white rook
	0x581B, 0x2E99, 0x733E, 0x6F79, 0xF308, 0xFC5C, 0x5AE4, 0x2380,
	0x9FE8, 0x13C0, 0xEF6E, 0x21B1, 0x4537, 0x54D2, 0xA9FF, 0x1E65,
	0x5D1C, 0x084E, 0xD4E2, 0xAB26, 0x94A4, 0x91C9, 0x46D2, 0x4E69,
	0xFC41, 0xD8A1, 0xB380, 0x5A3C, 0x4425, 0xF900, 0xC920, 0x7338,
	0xC005, 0x2DFD, 0xB956, 0x39E8, 0xD084, 0xDAB0, 0xCD4D, 0x9702,
	0x272B, 0xD1BF, 0x9817, 0x9D89, 0x5D04, 0x37F6, 0xB4C4, 0x2222,
	0xEE8B, 0x0BF4, 0x3DB8, 0x657B, 0xA4DD, 0x9877, 0x91F4, 0xA7E0,
	0x9E91, 0xFD61, 0x146B, 0xF0B2, 0x21ED, 0xA241, 0x263E, 0x11A6,
	// black knight
	0x8D34, 0xF49C, 0x730E, 0xC759, 0xAFA6, 0xB142, 0x335C, 0x63E8,
	0x75DC, 0x7FAC, 0xD5F5, 0xA22B, 0x6AB6, 0x58DE, 0xD099, 0x4703,
	0x7BD3, 0x2B8F, 0xC8B7, 0xFC0D, 0xC0DE, 0xB28D, 0x922A, 0xE1AE,
	0xB260, 0x01F5, 0x1F06, 0x6482, 0x0611, 0xA30E, 0x1CEE, 0xC93F,
	0xA9FD, 0x085D, 0x4485, 0x379A, 0x2CE7, 0xA494, 0x7CBC, 0x8430,
	0x8DD4, 0xF0AA, 0xB36F, 0x8735, 0xCF9A, 0xB25A, 0x25C8, 0x8EEA,
	0xBC49, 0x547A, 0xE6C7, 0xA643, 0x65E1, 0x403D, 0xC596, 0x54AE,
	0xEEC9, 0xF4CF, 0x1B61, 0xA72D, 0x520A, 0x6DDA, 0x1A56, 0x0BB6,
	// white knight
	0x2A23, 0xF109, 0x8CC2, 0x81AB, 0x9B00, 0x3F4E, 0x59E8, 0x1C0A,
	0x4A2F, 0xAB9D, 0x9C50, 0xD21F, 0x294B, 0xEE44, 0xD594, 0x4DD2,
	0x9102, 0x5EFA, 0xCC98, 0x96EA, 0x8837, 0x5B97, 0x563C, 0xDF13,
	0xA172, 0x98E8, 0x499D, 0xDBB6, 0x4DE6, 0x5E74, 0xD770, 0x5AF9,
	0x5583, 0xBFB3, 0xECE8, 0xF379, 0xAA10, 0xB068, 0x79C4, 0x6D84,
	0x5416, 0xE5C8, 0x1D48, 0xDF95, 0x4EF3, 0x05B5, 0xB1A5, 0x6C41,
	0x11B4, 0x55FC, 0x1638, 0x664C, 0x4D1B, 0x5E02, 0x453A, 0xD0A6,
	0x5C6A, 0x51E4, 0x86F5, 0xFCCD, 0x5D4B, 0x374D, 0x3EDF, 0xFFB9,
	// black bishop
	0x3F11, 0x102A, 0x8E83, 0x17A9, 0x384E, 0x0D2B, 0x9B62, 0x5782,
	0x7C65, 0x35BD, 0x6B86, 0x451B, 0x41D8, 0x3278, 0xF17B, 0xE08E,
	0x3F81, 0x9F39, 0x6EEE, 0xDE72, 0x76E6, 0x8D83, 0x166C, 0x45EF,
	0x0EED, 0x34CA, 0x29E7, 0x7466, 0xF0E8, 0x07F7, 0x7797, 0xBF19,
	0x1937, 0x305B, 0xD6DD, 0xE9B4, 0x1698, 0x7116, 0xF09C, 0x1B7C,
	0x550C, 0x867D, 0xD190, 0xC941, 0x8F42, 0x399B, 0x3AF5, 0x9F1B,
	0x71D1, 0xDE5C, 0xF251, 0xAE42, 0xC2F2, 0xCAA8, 0x1A42, 0x5D74,
	0x7935, 0x7652, 0x9A48, 0x6F26, 0x4841, 0x7F32, 0xE3D1, 0xF5B6,
	// white bishop
	0xF4C4, 0x12DE, 0x4303, 0xA47D, 0x5396, 0x5BF9, 0xE71C, 0xCB4F,
	0xE67D, 0x122C, 0x2F34, 0x1071, 0x2B39, 0x85AA, 0xF4A7, 0xE716,
	0xCEFB, 0x3049, 0x3F33, 0x8B58, 0x9B8C, 0xCA72, 0xEBC5, 0x5252,
	0xF838, 0x6BBD, 0x27D1, 0xB3CA, 0xDC3C, 0xBAB2, 0xCEC6, 0xC7D6,
	0xBAAD, 0xAA5B, 0xCFB1, 0xEB63, 0xFB38, 0x952B, 0xB649, 0xCC59,
	0x5EDF, 0x18FC, 0x62BE, 0x9092, 0x36B2, 0xA533, 0x35F3, 0x9983,
	0x92DD, 0xBB06, 0x5F99, 0x9680, 0x416C, 0x2452, 0x2EF1, 0x747A,
	0xCCEA, 0xD460, 0x3370, 0x2520, 0xF1FA, 0xE44F, 0x7020, 0xEE57,
	// black queen
	0x1F6B, 0xA9F0, 0x9763, 0x4AAD, 0x72C4, 0x0271, 0xD471, 0xDBCA,
	0x902A, 0x47E8, 0x09DE, 0x52A5, 0x77E7, 0xFD2C, 0xD6F8, 0xC5C5,
	0xD936, 0xD681, 0xD954, 0x459A, 0xCC8F, 0x0D05, 0x9C95, 0x8C78,
	0x5F6D, 0x9F32, 0x8E9F, 0xC2B6, 0xA7DD, 0x3787, 0x65AD, 0x3535,
	0xADB3, 0x1073, 0x6D40, 0xDC15, 0xD9BF, 0x24E1, 0xCD34, 0x867F,
	0x5E55, 0xC77D, 0xB212, 0x111D, 0xFC60, 0xCE30, 0xB2EA, 0x9D50,
	0xAC9A, 0xD4E7, 0x3A3F, 0xBDA4, 0x37D0, 0xBF30, 0x4F61, 0xCFCD,
	0xB4B3, 0xBF46, 0x1A0F, 0x0763, 0xBE09, 0x03EE, 0x9BC2, 0x9EFD,
	// white queen
	0x0C0E, 0x1516, 0xD3FB, 0xFDC4, 0xC050, 0x1315, 0x5C8A, 0x781D,
	0x758A, 0xA608, 0xE8E1, 0x1C9E, 0xFCBC, 0x3A03, 0x5DD6, 0x3251,
	0xE352, 0x84A4, 0xD801, 0xE773, 0x8892, 0xF195, 0x8BA5, 0x1D11,
	0x7940, 0x381A, 0x1EA5, 0xC738, 0x9FFD, 0x7473, 0x9C6A, 0x00A4,
	0x1344, 0x347C, 0x6212, 0x715D, 0xF744, 0xF43E, 0x11AA, 0xBDB6,
	0xE631, 0x3858, 0x7A8C, 0x225A, 0xD7CE, 0x076A, 0x8B1F, 0xA836,
	0x3D7A, 0x398C, 0xF12A, 0x138E, 0x5A4B, 0x29C6, 0x6761, 0xC296,
	0x01F0, 0x1C44, 0x74E8, 0xBC4E, 0xEE0B, 0x71C4, 0xAF60, 0xD881,
	// black king
	0xF408, 0x6C6D, 0x196B, 0xCF55, 0x4547, 0x2050, 0xC1EB, 0x8EBB,
	0xFB72, 0xD729, 0xCB0D, 0x558E, 0x1A29, 0x1E54, 0x9FBA, 0xD7FB,
	0x1D92, 0xD946, 0x23F0, 0x1965, 0x517E, 0xAAD4, 0x44DC, 0x0122,
	0x94FE, 0x763C, 0xD784, 0x2D30, 0x0CB3, 0x6825, 0xC0CD, 0xD4E3,
	0x785B, 0xA542, 0xB823, 0xAC00, 0xF294, 0x88FE, 0xA06A, 0xB036,
	0x1D00, 0x97CA, 0xE20F, 0x8EBE, 0x1CFE, 0x8C1E, 0x9C0C, 0xCAD9,
	0xC7CD, 0x2E40, 0x5FEB, 0x98E0, 0xDE75, 0x88C9, 0xD090, 0x3999,
	0xFC8F, 0x7111, 0xF698, 0x8FA7, 0xCD77, 0x3722, 0x02BD, 0xB8AC,
	// white king
	0xAAA1, 0x9527, 0x3C94, 0xC9CE, 0x9D5B, 0x02D4, 0x8E3D, 0xCE9E,
	0x5C1D, 0xF47E, 0x60B7, 0x6450, 0x4490, 0xAF89, 0x0C59, 0x9AD2,
	0xB893, 0x7D14, 0x9897, 0x51E1, 0xD6E4, 0x1CBF, 0x3737, 0x4311,
	0xDAF7, 0x052B, 0x969B, 0x6C5A, 0xAD91, 0x2B1B, 0xC154, 0xD910,
	0xC2B9, 0x8558, 0x3B35, 0x8ECD, 0x0F48, 0xD83B, 0x0968, 0x7DCA,
	0x93AE, 0x622C, 0xCB48, 0x2739, 0x8659, 0x3779, 0x33B2, 0x9C8A,
	0xB8F5, 0x8B87, 0x19E8, 0x310C, 0x5208, 0x32B8, 0xB192, 0x91AF,
	0xAE09, 0x7FC6, 0xECFF, 0xB466, 0xE2E6, 0x8665, 0x55D7, 0xAE87,
	// black pawn
	0x61C1, 0x6EB8, 0x5E2D, 0xBC89, 0xAAB4, 0x1D3C, 0xA66E, 0x7DF0,
	0x5C4D, 0xAC41, 0xA14B, 0xA810, 0xFB55, 0xDC3D, 0xB331, 0xB7B9,
	0xBAB0, 0x4DCC, 0xA8F2, 0xB722, 0x4ABD, 0x2926, 0xF0E7, 0xE7A5,
	0x1B3F, 0x9A46, 0x80B8, 0x9A80, 0x97D0, 0xCB0E, 0x026D, 0x4D2C,
	0xD879, 0xD36C, 0xC028, 0xE0D5, 0x1453, 0x835E, 0x49F7, 0x170E,
	0xDD3C, 0xF588, 0x426C, 0xCF12, 0xCB11, 0xD73E, 0x95C8, 0x7347,
	0xA301, 0x8899, 0xE813, 0x8BCC, 0xE032, 0xD217, 0x62B0, 0xB0FE,
	0xF74F, 0x1F0F, 0x5E89, 0x3F3E, 0x1D59, 0xCE54, 0xCCDC, 0x0EB5,
	// white pawn
	0x6373, 0x3085, 0x1113, 0xC620, 0x31EF, 0x3609, 0xE03C, 0x05E5,
	0x9C4B, 0xB929, 0xABA6, 0xB87B, 0x52B8, 0x3AA6, 0x127E, 0xB307,
	0x9F27, 0x065A, 0x6B42, 0x759E, 0xCCF6, 0x968D, 0xBB91, 0x4472,
	0xEB97, 0x546D, 0x2588, 0xE228, 0x1951, 0x3D34, 0x16A0, 0x8633,
	0x79A6, 0x0443, 0xCC52, 0x8CF3, 0x51F3, 0x112F, 0xBB37, 0x7061,
	0x286E, 0x40E8, 0xC161, 0xEFF6, 0x6FD0, 0x62A3, 0xE430, 0x6EEC,
	0x582D, 0xBC06, 0x746C, 0x2FA8, 0xB0F0, 0x0768, 0xEEDD, 0x1636,
	0xFD97, 0x0098, 0xDD93, 0xC1C9, 0x2517, 0x46C4, 0xB3E9, 0x2C8F,
};

// castling rights, indexed by the whole four bit mask
static const unsigned int sc_castleKey[16] =
{
	0x7D2D, 0xD3C0, 0xAE01, 0xC0DD, 0x928C, 0xF421, 0x4B90, 0xEB8B,
	0x83F0, 0x44C4, 0xB6DB, 0x7901, 0xA9D9, 0xF460, 0x03D2, 0x2C53,
};

// en passant target, by file
static const unsigned int sc_epKey[8] =
	{ 0x6953, 0x8F17, 0xF177, 0xC98E, 0x6602, 0x2D78, 0xFAEC, 0xC8F3 };

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

	// an empty board is a position like any other, and starting the history
	// here means a caller that sets pieces up and forgets eng_HashReset gets
	// an empty history rather than the last game's
	eng_HashReset();
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
	// and the position history have to be rebuilt rather than adjusted
	eval_Refresh();
	eng_HashReset();
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
// One piece on one square.  Index is the piece 1..6, then the colour, then
// the 0..63 tile - the same tile numbering the UI uses, so the table is 64
// wide rather than 128 and the off board halves of 0x88 cost nothing
static unsigned int pieceKey(char piece, char sq)
{
	return sc_zobrist[(((piece & PIECE_DATA) - 1) << 7) +
	                  ((piece & PIECE_WHITE) ? 64 : 0) +
	                  ENG_TO_TILE(sq)];
}

/*-----------------------------------------------------------------------*/
// The full position, which is the piece placement plus the two things that
// make an identical looking board a different position.
//
// Castling rights have to be in here.  A king that steps off e1 and back has
// the same pieces on the same squares as before it moved, and is not the same
// position - it can no longer castle.  That is a shuffle, which is exactly
// the case this whole feature exists to judge, so getting it wrong would
// misfire in the place it matters most
static unsigned int positionKey(void)
{
	unsigned int key = geHashKey ^ sc_castleKey[geCastle];

	if(ENG_NO_SQUARE != geEP)
		key ^= sc_epKey[ENG_FILE(geEP)];

	return key;
}

/*-----------------------------------------------------------------------*/
// What "move" does to geHashKey.  Deliberately the same shape as
// eval_MoveDelta, case for case - mover, promotion, the en passant victim
// that is not on the target square, and the rook that castling moves as well.
// The two are wrong in the same way if they are wrong at all, which makes
// them easy to check against each other.
//
// XOR is its own inverse, so eng_Unmake applies this identically rather than
// needing a second function that undoes it
static unsigned int hashDelta(const t_engMove *move, char piece, char captured)
{
	char to = move->m_to, flags = move->m_flags;
	char promote = flags & ENG_MF_PROMO;
	char white = piece & PIECE_WHITE;
	unsigned int delta;

	delta = pieceKey(promote ? (promote | white) : piece, to) ^
	        pieceKey(piece, move->m_from);

	if(NONE != (captured & PIECE_DATA))
	{
		char victim = (flags & ENG_MF_ENPASSANT) ? (white ? to + 16 : to - 16) : to;

		delta ^= pieceKey(captured, victim);
	}

	if(flags & ENG_MF_CASTLE)
	{
		char rook = ROOK | white;

		if(flags & ENG_MF_CASTLE_K)
			delta ^= pieceKey(rook, to - 1) ^ pieceKey(rook, to + 1);
		else
			delta ^= pieceKey(rook, to + 1) ^ pieceKey(rook, to - 2);
	}

	return delta;
}

/*-----------------------------------------------------------------------*/
unsigned int eng_HashOfBoard(void)
{
	unsigned int key = 0;
	char sq;

	for(sq = 0; sq < 0x78; ++sq)
	{
		if(ENG_OFFBOARD(sq))
			continue;
		if(NONE != (geBoard[sq] & PIECE_DATA))
			key ^= pieceKey(geBoard[sq], sq);
	}

	return key;
}

/*-----------------------------------------------------------------------*/
void eng_HashReset(void)
{
	geHashKey = eng_HashOfBoard();

	// The position as it stands goes in as the first entry, so that returning
	// to it counts.  Without that the opening position - or whatever position
	// a FEN was loaded at - is the one position in the game that can be
	// repeated for free
	sc_hashTop = 0;
	sc_hashValid = 0;
	su_hashRing[sc_hashTop++] = positionKey();
	sc_hashValid = 1;
}

/*-----------------------------------------------------------------------*/
char eng_IsRepetition(char needed)
{
	unsigned int key = positionKey();
	char limit = sc_hashValid - 1;			// the newest entry is this position
	char back, seen = 0;

	if(geHalfmove < limit)
		limit = geHalfmove;

	for(back = 2; back <= limit; back += 2)
	{
		if(su_hashRing[(char)(sc_hashTop - 1 - back) & HASH_MASK] == key)
		{
			if(++seen >= needed)
				return 1;
		}
	}

	return 0;
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
	geHashKey ^= hashDelta(move, piece, undo->m_captured);

	// and record where we now are.  This runs for search moves as well as
	// real ones, which is the point: the ring is the game's history and the
	// current search line at the same time
	su_hashRing[sc_hashTop & HASH_MASK] = positionKey();
	++sc_hashTop;
	if(sc_hashValid < HASH_RING)
		++sc_hashValid;
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
	geHashKey ^= hashDelta(move, moved, undo->m_captured);

	--sc_hashTop;
	if(sc_hashValid)
		--sc_hashValid;
}
