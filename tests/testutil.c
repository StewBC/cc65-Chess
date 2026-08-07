/*
 *	testutil.c
 *	cc65 Chess - test support
 */

#include <stdio.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "testutil.h"

static const char sc_pieceChar[] = { '.','R','N','B','Q','K','P' };

/*-----------------------------------------------------------------------*/
char test_Square(const char *algebraic)
{
	// tile 0 is a8, so rank 8 maps to row 0
	return (char)((('8' - algebraic[1]) * 8) + (algebraic[0] - 'a'));
}

/*-----------------------------------------------------------------------*/
void test_TileName(char tile, char *out3)
{
	out3[0] = 'a' + (tile & 7);
	out3[1] = '8' - (tile / 8);
	out3[2] = '\0';
}

/*-----------------------------------------------------------------------*/
// The fullmove number is always 1: nothing that reads these cares, and the
// engine does not track it
void test_EngineGetFEN(char side, char *out)
{
	char row, file, *p = out;

	for(row = 0; row < 8; ++row)
	{
		char empty = 0;

		for(file = 0; file < 8; ++file)
		{
			char piece = geBoard[(row << 4) | file];
			char type = piece & PIECE_DATA;

			if(NONE == type)
			{
				++empty;
				continue;
			}
			if(empty)
			{
				*p++ = '0' + empty;
				empty = 0;
			}
			*p++ = (piece & PIECE_WHITE) ? sc_pieceChar[type]
			                             : sc_pieceChar[type] - 'A' + 'a';
		}
		if(empty)
			*p++ = '0' + empty;
		if(row < 7)
			*p++ = '/';
	}

	*p++ = ' ';
	*p++ = (SIDE_WHITE == side) ? 'w' : 'b';
	*p++ = ' ';

	if(!geCastle)
		*p++ = '-';
	else
	{
		if(geCastle & ENG_CASTLE_WK) *p++ = 'K';
		if(geCastle & ENG_CASTLE_WQ) *p++ = 'Q';
		if(geCastle & ENG_CASTLE_BK) *p++ = 'k';
		if(geCastle & ENG_CASTLE_BQ) *p++ = 'q';
	}

	*p++ = ' ';
	if(ENG_NO_SQUARE == geEP)
		*p++ = '-';
	else
	{
		char name[3];
		test_TileName(ENG_TO_TILE(geEP), name);
		*p++ = name[0];
		*p++ = name[1];
	}

	sprintf(p, " %d 1", geHalfmove);
}

/*-----------------------------------------------------------------------*/
void test_DumpBoard(const char *tag)
{
	char row, file;

	printf("--- %s  (kings b=%02x w=%02x, ep=%02x, castle=%x, halfmove=%d)\n",
	       tag, geKing[0], geKing[1], geEP, geCastle, geHalfmove);

	for(row = 0; row < 8; ++row)
	{
		printf("  %d ", 8 - row);
		for(file = 0; file < 8; ++file)
		{
			char piece = geBoard[(row << 4) | file];
			char c = sc_pieceChar[piece & PIECE_DATA];

			if(!(piece & PIECE_WHITE) && (piece & PIECE_DATA))
				c = c - 'A' + 'a';
			printf("%c", (piece & PIECE_DATA) ? c : '.');
		}
		printf("\n");
	}
	printf("    abcdefgh\n");
}
