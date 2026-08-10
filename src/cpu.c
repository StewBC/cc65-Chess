/*
 *	cpu.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 *	All that is left of this file is the turn: ask the search for a move, play
 *	it, redraw.  The scoring heuristics, the sorted move arrays and the
 *	single-line "sub tree" walk are gone - see search.c.
 *
 *	Except for one thing, and it is here rather than in the search on purpose:
 *	the first move of a game, when the engine has White and nothing has been
 *	played yet, comes from a four entry table instead of from a search.
 */

#include "types.h"
#include "globals.h"
#include "engine.h"
#include "search.h"
#include "board.h"
#include "undo.h"
#include "cpu.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
// The opening move, when the engine has White and the board is untouched.
//
// Four moves, as 0..63 tiles: e4, d4, Nf3, c4.  Between them they account for
// nearly all master practice and they give four games that feel different from
// each other - king's pawn, queen's pawn, Reti, English.
//
// This is a table and not a search because the search is *wrong* here, not
// merely slow.  At 400 nodes the evaluation rates b1c3 and g1f3 equal best and
// puts e4 and d4 ten centipawns behind, which is the engine's opinion and not
// chess's; a first move is one of the few places where a table of known answers
// beats anything this engine can work out.  It is also the cheapest possible
// speedup: no search at all, where level 4 would have spent about fifteen
// minutes on a stock C64 to play a move that was never in doubt.
//
// gSkillLevel is deliberately not consulted.  A weak level is weak because it
// searches less, not because it should open worse
// Tile 0 is a8, so rank 1 is the *last* row: tile = (8 - rank) * 8 + file.
// Getting that backwards puts black's pawns in the table, where they match
// nothing and the engine quietly searches instead - which is what the first
// draft of this did, and what tests/opening.c caught
static const char sc_openingBook[] =
{
	52, 36,			// e2 e4
	51, 35,			// d2 d4
	62, 45,			// g1 f3
	50, 34,			// c2 c4
};

// A power of two on purpose: the roll is masked rather than divided, because a
// modulo on a 6502 is a subroutine call and this is the only division in the
// game loop.  Adding entries means going to eight, not five
#define BOOK_MOVES		4
#define BOOK_MASK		(BOOK_MOVES - 1)

/*-----------------------------------------------------------------------*/
// Look the roll up in the generator's own output rather than trusting the
// table to describe a legal move.  One generation is nothing next to the
// search it replaces, and it means a typo in the table above cannot put an
// illegal move on the board - the entry simply never matches and the caller
// searches as usual
static char cpu_BookMove(t_engMove *move)
{
	// The start position has exactly 20 legal moves and this function is only
	// ever called there, so 24 is margin rather than a guess.  A move list on
	// the stack is normally forbidden here - cc65 gives a function 256 bytes of
	// locals and a full width list blows it - but 24 entries is 96 bytes and
	// the size cannot grow with the position, because there is only one
	// position this ever sees
	t_engMove moves[24];
	char count, i, roll, from, to;

	roll = (search_Random() & BOOK_MASK) << 1;
	from = ENG_FROM_TILE(sc_openingBook[roll]);
	to   = ENG_FROM_TILE(sc_openingBook[roll + 1]);

	count = eng_GenMoves(SIDE_WHITE, moves, sizeof(moves) / sizeof(moves[0]));
	for(i = 0; i < count; ++i)
		if(moves[i].m_from == from && moves[i].m_to == to)
		{
			*move = moves[i];
			return 1;
		}

	return 0;
}

/*-----------------------------------------------------------------------*/
char cpu_Play(char side)
{
	t_searchResult result;
	char outcome, from, to;

	// The table only ever answers one question: what to play as White into an
	// untouched board.  Anything else - the engine has Black, or a move has
	// been played, or nobody seeded the randomiser - and this is the engine
	// that was always here.  undo_CanUndo is false exactly while no move has
	// been made, so the game needs no new state to know which case it is in
	if(SIDE_WHITE == side && !undo_CanUndo() && search_Seeded() &&
	   cpu_BookMove(&result.m_move))
	{
		result.m_haveMove = 1;
	}
	else
	{
		plat_ShowMessage(gszThinking, HCOLOR_VALID);

		search_Best(side, gcSearchSkill[gSkillLevel].m_depth,
		            gcSearchSkill[gSkillLevel].m_nodes, &result);

		plat_ClearMessage();
	}

	// No legal move: mate or stalemate, whichever the position says
	if(!result.m_haveMove)
		return search_Outcome(side);

	from = ENG_TO_TILE(result.m_move.m_from);
	to = ENG_TO_TILE(result.m_move.m_to);

	outcome = board_ApplyMove(&result.m_move, side);

	plat_DrawSquare(from);
	plat_DrawSquare(to);

	// castling moves a rook too, and en passant clears a third tile, so the
	// whole rank the move touched gets redrawn rather than tracking squares
	if(result.m_move.m_flags & (ENG_MF_CASTLE | ENG_MF_ENPASSANT))
		plat_DrawBoard(0);

	frontend_LogMove(0);

	return outcome;
}
