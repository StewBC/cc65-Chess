/*
 *	genbook.c
 *	cc65 Chess - test support
 *
 *	Writes an EPD opening book on stdout, for a match runner to start games
 *	from.  The engine is deterministic, so without a book a match is one game
 *	repeated N times - see the note at the top of tests/uci.c.
 *
 *	The positions are generated the same way tests/match.c generates its own,
 *	on purpose: a few plies of random legal non-capturing moves, one fixed seed
 *	per opening.  That keeps an external match commensurable with the internal
 *	one, and it is reproducible without a page of hand-typed FENs to get wrong.
 *
 *	  ./genbook [count] [plies] > book.epd
 */

#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "testutil.h"

/*-----------------------------------------------------------------------*/
int main(int argc, char **argv)
{
	int count = (argc > 1) ? atoi(argv[1]) : 256;
	int plies = (argc > 2) ? atoi(argv[2]) : 4;
	int i;

	for(i = 0; i < count; ++i)
	{
		char side = SIDE_WHITE, fen[90];
		int ply;

		srand(1000 + i);
		eng_SetStartPosition();

		for(ply = 0; ply < plies; ++ply)
		{
			t_engMove moves[ENG_MAX_MOVES];
			t_engUndo undo;
			char n = eng_GenMoves(side, moves, ENG_MAX_MOVES);
			char tries, done = 0;

			// no captures: a book position where one side is already a piece
			// up measures the opening set rather than the engines
			for(tries = 0; tries < n * 2 && !done; ++tries)
			{
				char pick = rand() % n;

				{
					char wasInCheck = eng_InCheck(side);

					eng_Make(&moves[pick], &undo);
					if(eng_LeavesInCheck(side, &moves[pick], wasInCheck) ||
					   NONE != (undo.m_captured & PIECE_DATA))
						eng_Unmake(&moves[pick], &undo);
					else
						done = 1;
				}
			}
			if(!done)
				break;
			side = 1 - side;
		}

		test_EngineGetFEN(side, fen);
		printf("%s\n", fen);
	}
	return 0;
}
