/*
 *	gamefuzz.c
 *	cc65 Chess - test support
 *
 *	The old fuzzer drove board_ProcessAction and checked the attack database.
 *	Both are gone, so this drives the game the way the UI does - board_ApplyMove,
 *	undo_Undo, undo_Redo - and checks what still has to hold:
 *
 *	  - material never increases and there is exactly one king a side
 *	  - gChessBoard, which every port draws from, matches the engine board
 *	  - every undo reproduces the position before that move, byte for byte
 *	  - every redo reproduces the position after it
 *	  - the incremental evaluation still agrees with a full recount
 *	  - so do the incremental position hash and the game phase
 *
 *	Castling, en passant and promotion are preferred whenever available, since
 *	random play almost never reaches them on its own.  That matters most for the
 *	last check: those three are exactly the moves whose eval delta is not just
 *	"a piece left one square and arrived on another".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "eval.h"
#include "undo.h"
#include "board.h"
#include "search.h"
#include "testutil.h"

#define MAX_PLIES	120

static char sc_snapshots[MAX_PLIES][128];
static t_engMove st_played[MAX_PLIES];
static char sc_probeBoard[128];

/*-----------------------------------------------------------------------*/
static int compareBoard(const char *want, int ply, const char *tag, int game)
{
	char sq;

	for(sq = 0; sq < 0x78; ++sq)
	{
		if(ENG_OFFBOARD(sq))
			continue;
		if(want[sq] != geBoard[sq])
		{
			char name[3];
			test_TileName(ENG_TO_TILE(sq), name);
			printf("    game %d %s ply %d: %s expected %02x got %02x\n",
			       game, tag, ply, name, want[sq] & 0xff, geBoard[sq] & 0xff);
			return 1;
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// gChessBoard is what every platform file draws from, so it has to agree with
// the engine after anything that moves a piece
static int checkDisplayMirror(int game, int ply)
{
	char sq, tile;

	for(sq = 0; sq < 0x78; ++sq)
	{
		if(ENG_OFFBOARD(sq))
			continue;
		tile = ENG_TO_TILE(sq);
		if(gChessBoard[tile >> 3][tile & 7] != geBoard[sq])
		{
			printf("    game %d ply %d: display mirror out of step at tile %d\n",
			       game, ply, tile);
			return 1;
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static int checkMaterial(int game, int ply, int *prevBlack, int *prevWhite)
{
	char sq;
	int black = 0, white = 0, kb = 0, kw = 0;

	for(sq = 0; sq < 0x78; ++sq)
	{
		char piece = geBoard[sq];

		if(ENG_OFFBOARD(sq) || NONE == (piece & PIECE_DATA))
			continue;
		if(piece & PIECE_WHITE) { ++white; if(KING == (piece & PIECE_DATA)) ++kw; }
		else                    { ++black; if(KING == (piece & PIECE_DATA)) ++kb; }
	}

	if(black > *prevBlack || white > *prevWhite || kb != 1 || kw != 1)
	{
		printf("    game %d ply %d: material black %d->%d white %d->%d kings %d/%d\n",
		       game, ply, *prevBlack, black, *prevWhite, white, kb, kw);
		return 1;
	}
	if(KING != (geBoard[geKing[0]] & PIECE_DATA) || KING != (geBoard[geKing[1]] & PIECE_DATA))
	{
		printf("    game %d ply %d: king tracker wrong (b=%02x w=%02x)\n",
		       game, ply, geKing[0], geKing[1]);
		return 1;
	}

	*prevBlack = black;
	*prevWhite = white;
	return 0;
}

/*-----------------------------------------------------------------------*/
// The evaluation is a running total now, adjusted by eng_Make and eng_Unmake.
// A wrong delta does not crash or corrupt anything - it just quietly makes the
// engine play to a score that drifts further from the truth every move, which
// is close to undetectable by any other means.  So check it against the full
// recount after everything: a move, an undo, a redo
static int checkEvalScore(int game, int ply, const char *tag)
{
	int running = geEvalScore;

	eval_Refresh();
	if(running != geEvalScore)
	{
		printf("    game %d %s ply %d: eval drifted, running %d full %d\n",
		       game, tag, ply, running, geEvalScore);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// The other two running totals: non-pawn material, which says how far into the
// endgame the position is, and the endgame score difference blended in by it.
// Drift here corrupts nothing either - it just evaluates the wrong endgame
static int checkPhase(int game, int ply, const char *tag)
{
	int phase = gePhase, endBonus = geEvalEnd;

	eval_Refresh();
	if(phase != gePhase)
	{
		printf("    game %d %s ply %d: phase drifted, running %d full %d\n",
		       game, tag, ply, phase, gePhase);
		return 1;
	}
	if(endBonus != geEvalEnd)
	{
		printf("    game %d %s ply %d: endgame total drifted, running %d full %d\n",
		       game, tag, ply, endBonus, geEvalEnd);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// Same argument as the evaluation, and the same failure mode: the hash is
// carried along by eng_Make and eng_Unmake, a wrong delta breaks nothing
// visibly, and the only symptom is repetition detection answering about a
// position that is not on the board.  Recomputed here without touching the
// history, so this checks the key and never hides a bug in the ring
static int checkHashKey(int game, int ply, const char *tag)
{
	unsigned int full = eng_HashOfBoard();

	if(geHashKey != full)
	{
		printf("    game %d %s ply %d: hash drifted, running %04X full %04X\n",
		       game, tag, ply, geHashKey, full);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static int checkHistoryKey(int game, int ply, const char *tag)
{
	if(!eng_HistoryMatchesPosition())
	{
		printf("    game %d %s ply %d: newest history key is not current\n",
		       game, tag, ply);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// A no-history move must be exactly self-reversing and must never touch the
// hash or any ring slot.  Return -1 for a broken round trip, else whether the
// move was legal.
static int probeLegal(const t_engMove *move, char side, int game, int ply)
{
	t_engUndo undo;
	unsigned int hash = geHashKey;
	unsigned int history = eng_HistoryStateDigest();
	int score = geEvalScore, end = geEvalEnd, phase = gePhase;
	char ep = geEP, castle = geCastle, halfmove = geHalfmove;
	char kingBlack = geKing[SIDE_BLACK], kingWhite = geKing[SIDE_WHITE];
	char legal;

	memcpy(sc_probeBoard, geBoard, 128);
	eng_HistoryEnable(0);
	eng_Make(move, &undo);
	legal = !eng_IsAttacked(geKing[side], 1 - side);
	eng_Unmake(move, &undo);
	eng_HistoryEnable(1);

	if(memcmp(sc_probeBoard, geBoard, 128) ||
	   hash != geHashKey || history != eng_HistoryStateDigest() ||
	   score != geEvalScore || end != geEvalEnd || phase != gePhase ||
	   ep != geEP || castle != geCastle || halfmove != geHalfmove ||
	   kingBlack != geKing[SIDE_BLACK] || kingWhite != geKing[SIDE_WHITE])
	{
		printf("    game %d probe ply %d: no-history make/unmake changed state\n",
		       game, ply);
		return -1;
	}

	return legal;
}

/*-----------------------------------------------------------------------*/
int test_RunGameFuzz(int seed, int games, int verbose)
{
	int game, failures = 0, specials = 0;

	for(game = 0; game < games; ++game)
	{
		t_engMove moves[ENG_MAX_MOVES];
		char side = SIDE_WHITE;
		int ply, plies = 0, k, prevBlack = 16, prevWhite = 16;

		srand(seed + game);
		board_Init();
		undo_Init();

		for(ply = 0; ply < MAX_PLIES; ++ply)
		{
			char count, i, chosen = 0xFF;

			if(OUTCOME_OK != search_Outcome(side))
				break;

			count = eng_GenMoves(side, moves, ENG_MAX_MOVES);

			// prefer the special moves heavily when they are available
			for(i = 0; i < count && 0xFF == chosen; ++i)
			{
				if((moves[i].m_flags & (ENG_MF_CASTLE | ENG_MF_ENPASSANT | ENG_MF_PROMO)) &&
				   (rand() % 10) < 8)
				{
					int legal = probeLegal(&moves[i], side, game, ply);

					if(legal < 0)
					{
						++failures;
						goto nextGame;
					}
					if(legal)
						chosen = i;
					if(0xFF != chosen)
						++specials;
				}
			}

			for(i = 0; i < count * 2 && 0xFF == chosen; ++i)
			{
				char pick = rand() % count;
				int legal = probeLegal(&moves[pick], side, game, ply);

				if(legal < 0)
				{
					++failures;
					goto nextGame;
				}
				if(legal)
					chosen = pick;
			}

			if(0xFF == chosen)
				break;

			memcpy(sc_snapshots[plies], geBoard, 128);
			st_played[plies] = moves[chosen];

			board_ApplyMove(&moves[chosen], side);
			++plies;

			if(checkMaterial(game, ply, &prevBlack, &prevWhite) ||
			   checkDisplayMirror(game, ply) ||
			   checkEvalScore(game, ply, "move") ||
			   checkHashKey(game, ply, "move") ||
			   checkHistoryKey(game, ply, "move") ||
			   checkPhase(game, ply, "move"))
			{
				++failures;
				goto nextGame;
			}

			side = 1 - side;
		}

		// unwind, then replay
		k = plies;
		while(k)
		{
			--k;
			undo_Undo();
			if(compareBoard(sc_snapshots[k], k, "undo", game) ||
			   checkEvalScore(game, k, "undo") ||
			   checkHashKey(game, k, "undo") ||
			   checkHistoryKey(game, k, "undo") ||
			   checkPhase(game, k, "undo"))
			{
				++failures;
				goto nextGame;
			}
		}

		for(k = 0; k < plies; ++k)
		{
			undo_Redo();
			if(checkEvalScore(game, k, "redo") ||
			   checkHashKey(game, k, "redo") ||
			   checkHistoryKey(game, k, "redo") ||
			   checkPhase(game, k, "redo") ||
			   (k + 1 < plies && compareBoard(sc_snapshots[k+1], k, "redo", game)))
			{
				++failures;
				goto nextGame;
			}
		}
nextGame: ;
	}

	printf("game fuzz: %d games from seed %d, %d special moves, %d failing\n",
	       games, seed, specials, failures);
	(void)verbose;
	return failures;
}
