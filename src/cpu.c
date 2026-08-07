/*
 *	cpu.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 *	All that is left of this file is the turn: ask the search for a move, play
 *	it, redraw.  The scoring heuristics, the sorted move arrays and the
 *	single-line "sub tree" walk are gone - see search.c.
 */

#include "types.h"
#include "globals.h"
#include "engine.h"
#include "search.h"
#include "board.h"
#include "cpu.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
char cpu_Play(char side)
{
	t_searchResult result;
	char outcome, from, to;

	plat_ShowMessage(gszThinking, HCOLOR_VALID);

	search_Best(side, gcSearchSkill[gSkillLevel].m_depth,
	            gcSearchSkill[gSkillLevel].m_nodes, &result);

	plat_ClearMessage();

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
