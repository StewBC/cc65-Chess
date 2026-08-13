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
 *	the first move of a game comes from a table instead of from a search.  Four
 *	entries when the engine has White and nothing has been played, and five
 *	entries of two replies each when it has Black and White has moved once.
 *
 *	The two tables are here for different reasons and it is worth not confusing
 *	them.  White's exists because the search is *wrong* about the first move and
 *	the measurement says so.  Black's exists because a deterministic engine with
 *	one reply plays one game, and a study that thought it had thirty-two black
 *	games against Sargon II had five.
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
// Black's reply, keyed on the move White actually played.  Six bytes an entry:
// White's move, then two replies, and a coin toss picks between them.
//
// This is here for a different reason than the white table above, and the
// reason is worth stating because the obvious one is mostly wrong.  It is *not*
// that the table knows better chess than the search.  Every entry was picked by
// playing it: 192 openings a reply, cc65 as black at 1,200 nodes against
// Stockfish at 30 and at 100 nodes, and only two of the five first moves
// produced a reply that beat the engine's own choice by more than the noise -
//
//		1.e4	d5   38.0%	against Nc6 30.3%
//		1.f4	e5   39.1%	against Nc6 31.4%
//		1.d4	Nf6  34.6%	against Nc6 32.8%	inside the noise
//		1.c4	d5   34.0%	against Nc6 32.8%	inside the noise
//		1.Nf3	d5   32.8%	against Nc6 31.5%	inside the noise
//
// That three of the five are flat is the finding, not a disappointment: which
// reply is played barely matters, so this table is not buying opening theory.
//
// What it is buying is that without it the engine answers a given first move
// with the same move forever, and the game that follows is the same game
// forever.  Fourteen of the thirty-two black games in the Sargon II study were
// one identical 103 ply loss, and all eighteen black losses came from two of
// the five openings that ever appeared; in the other fourteen games black
// scored 57%.  The twenty-five percent in doc/strength.md 5.1.6 is mostly one
// bad game weighted fourteen times.  Against Sargon this table took thirty-two
// black games from five distinct ones to twenty-four, the worst repeat from
// fourteen copies to five, and the score from 25.0% to 45.3% - in that order of
// importance.
//
// **The second reply to 1.e4 was d6 and it had to be measured twice.**  It is a
// reasonable move, it ranked second of eight on score against Stockfish, and it
// ranked *first* on a second instrument written specifically to count how many
// distinct games a reply produces.  Against Sargon it played twenty-one games
// that were two games, and lost all of them - the exact defect this table
// exists to remove, rebuilt.  e5 was chosen instead on the only thing the games
// themselves showed: a reply that forces an exchange (2.exd5 Qxd5, 2.exe5)
// makes the opponent diverge, and a closed one lets both programs replay one
// game.  Anything changed here needs the rig, because both desk proxies were
// confidently wrong - doc/strength.md 5.1.7.
//
// A first move with no entry here falls through to the search, which is what
// every first move did before this existed.
//
// Tile 0 is a8, so black's pieces are the *low* tiles - d7 is 11, not 51.  The
// white table above has the same trap the other way up and tests/opening.c
// caught it there; the same test now drives this one
static const char sc_blackBook[] =
{
	// White's move    reply         alternative
	52, 36,			11, 27,		12, 28,		// 1.e4  - d5, e5
	51, 35,			 6, 21,		11, 27,		// 1.d4  - Nf6, d5
	50, 34,			11, 27,		12, 28,		// 1.c4  - d5, e5
	62, 45,			11, 27,		 6, 21,		// 1.Nf3 - d5, Nf6
	53, 37,			12, 28,		 6, 21,		// 1.f4  - e5, Nf6
};

#define BLACK_BOOK_STRIDE	6

/*-----------------------------------------------------------------------*/
// Look the table's answer up in the generator's own output rather than
// trusting the table to describe a legal move.  One generation is nothing next
// to the search it replaces, and it means a typo in a table above cannot put an
// illegal move on the board - the entry simply never matches and the caller
// searches as usual
static char cpu_MatchMove(char side, char from, char to, t_engMove *move)
{
	// Both positions this is ever called from have exactly 20 legal moves -
	// the start position, and the start position after one white move, which
	// cannot check, capture or block anything of black's - so 24 is margin
	// rather than a guess.  A move list on the stack is normally forbidden here
	// - cc65 gives a function 256 bytes of locals and a full width list blows
	// it - but 24 entries is 96 bytes and the size cannot grow with the
	// position, because there are only two positions this ever sees
	t_engMove moves[24];
	char count, i;

	from = ENG_FROM_TILE(from);
	to   = ENG_FROM_TILE(to);

	count = eng_GenMoves(side, moves, sizeof(moves) / sizeof(moves[0]));
	for(i = 0; i < count; ++i)
		if(moves[i].m_from == from && moves[i].m_to == to)
		{
			*move = moves[i];
			return 1;
		}

	return 0;
}

/*-----------------------------------------------------------------------*/
// The tables, as one entry point so the game and tests/uci ask the same
// question of the same code.  ply is how many moves have been played - only 0
// and 1 are ever answered - and wFrom/wTo are white's first move, as 0..63
// tiles, when ply is 1
char cpu_BookMove(char side, char ply, char wFrom, char wTo, t_engMove *move)
{
	char roll, i;

	if(SIDE_WHITE == side)
	{
		if(ply)
			return 0;

		roll = (search_Random() & BOOK_MASK) << 1;
		return cpu_MatchMove(SIDE_WHITE, sc_openingBook[roll],
		                     sc_openingBook[roll + 1], move);
	}

	if(1 != ply)
		return 0;

	for(i = 0; i < sizeof(sc_blackBook); i += BLACK_BOOK_STRIDE)
		if(sc_blackBook[i] == wFrom && sc_blackBook[i + 1] == wTo)
		{
			// two replies an entry, so one bit of the roll picks the pair
			i += 2 + ((search_Random() & 1) << 1);
			return cpu_MatchMove(SIDE_BLACK, sc_blackBook[i],
			                     sc_blackBook[i + 1], move);
		}

	return 0;
}

/*-----------------------------------------------------------------------*/
char cpu_Play(char side)
{
	t_searchResult result;
	char outcome, from, to, ply = 0;

	// The tables answer two questions: what to play as White into an untouched
	// board, and how to answer White's first move as Black.  Anything else - a
	// game already under way, or nobody seeded the randomiser - and this is the
	// engine that was always here.
	//
	// The game needs no new state to know which case it is in.  undo_CanUndo is
	// false exactly while no move has been made, undo_FindUndoLine(1) is false
	// for exactly one move more than that, and (0) then loads White's move into
	// gTile for the black table to key on.  Those two calls do write the log
	// globals, which is harmless: plat_AddToLogWin walks the undo stack and
	// rebuilds them every time a port draws the log
	if(undo_CanUndo())
	{
		ply = undo_FindUndoLine(1) ? 2 : 1;
		if(1 == ply)
			undo_FindUndoLine(0);
	}

	if(ply < 2 && search_Seeded() &&
	   cpu_BookMove(side, ply, gTile[0], gTile[1], &result.m_move))
	{
		result.m_haveMove = 1;
	}
	else
	{
		plat_ShowMessage(gszThinking, HCOLOR_VALID);

		search_Best(side, gcSearchSkill[gSkillLevel].m_depth,
		            gcSearchSkill[gSkillLevel].m_nodes, &result);

		plat_ClearMessage();
		if(search_Interrupted())
			return OUTCOME_MENU;
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
