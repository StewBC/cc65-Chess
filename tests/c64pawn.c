/*
 *	c64pawn.c — print structure scores for a few FENs so the 6502 asm can be
 *	checked against the host C reference.
 *
 *	cl65 -t c64 -Oris -I../src -o c64pawn.prg \
 *	     ../src/engine.c ../src/eval.c ../src/search.c ../src/pawnstruct.s c64pawn.c
 */
#include <stdio.h>
#include "types.h"
#include "engine.h"
#include "eval.h"

static void show(const char *name)
{
	int s = eval_Position(SIDE_WHITE);
	printf("%s struct=%d score=%d\n", name, gePawnStruct, s);
}

int main(void)
{
	/* start */
	eng_SetStartPosition();
	show("start");

	/* white doubled e with neighbours: want -8 */
	/* set by hand: clear and place pieces */
	eng_Clear();
	geBoard[0x04] = KING;			/* e8 */
	geBoard[0x74] = KING | PIECE_WHITE;	/* e1 */
	geKing[0] = 0x04;
	geKing[1] = 0x74;
	geBoard[0x53] = PAWN | PIECE_WHITE;	/* d3 */
	geBoard[0x54] = PAWN | PIECE_WHITE;	/* e3 */
	geBoard[0x55] = PAWN | PIECE_WHITE;	/* f3 */
	geBoard[0x64] = PAWN | PIECE_WHITE;	/* e2 */
	eval_Refresh();
	show("wdoubled");

	/* white isolated a */
	eng_Clear();
	geBoard[0x04] = KING;
	geBoard[0x74] = KING | PIECE_WHITE;
	geKing[0] = 0x04;
	geKing[1] = 0x74;
	geBoard[0x60] = PAWN | PIECE_WHITE;
	eval_Refresh();
	show("wiso");

	/* black doubled+isolated h: want +40 */
	eng_Clear();
	geBoard[0x04] = KING;
	geBoard[0x74] = KING | PIECE_WHITE;
	geKing[0] = 0x04;
	geKing[1] = 0x74;
	geBoard[0x17] = PAWN;
	geBoard[0x27] = PAWN;
	eval_Refresh();
	show("bdh");

	printf("done.\n");
	for(;;)
		;
	return 0;
}
