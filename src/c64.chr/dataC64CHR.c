/*
 *	dataC64CHR.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "../types.h"
#include "dataC64CHR.h"

/*-----------------------------------------------------------------------*/
// C64 specific graphics for the chess pieces

// The comment shows an icon 16x24
// The data is 24 chars & 24 chars (2 * 8x24).	The second set of 24
// is the right-hand side 8-bits of the icon

// The pieces use bit pattern 11 as the transparent color, which uses
// the color from the color memory. Bit pattern 01 is the piece color
// and uses bgcolor1 for black pieces.	In code, 01 is switched to 10
// for white pieces. 10 uses bgcolor2 for its color
const char gfxTiles[PAWN][2*24] =
{
	{
		0xff,	/* XXXXXXXXXXXXXXXX */	// 0
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xdd,	/* XX-XXX-X-XXX-XXX */	
		0xdd,	/* XX-XXX-X-XXX-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	/* 48 */
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0x77,							
		0x77,							
		0x57,							
		0x57,							
		0x57,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x57,							
		0x57,							
	},									
	{									
		0xff,	/* XXXXXXXXXXXXXXXX */	// 1
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXX-XXXXX */	
		0xff,	/* XXXXXXXX-X-XXXXX */	
		0xfd,	/* XXXXXX-X-X-X-XXX */	
		0xf5,	/* XXXX-X-X-X-X-XXX */	
		0xf7,	/* XXXX-XXX-X-X-XXX */	
		0xd7,	/* XX-X-XXX-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xdd,	/* XX-XXX-X-X-XXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	/* 96 */
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xdf,							
		0x5f,							
		0x57,							
		0x57,							
		0x57,							
		0x57,							
		0x57,							
		0x57,							
		0x57,							
		0x57,							
		0x5f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x57,							
		0x57,							
	},									
	{									
		0xff,	/* XXXXXXXXXXXXXXXX */	// 2
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-XXXXXXXXX */	
		0xf7,	/* XXXX-XXXXX-XXXXX */	
		0xf7,	/* XXXX-XXX-X-XXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	/* 144 */
		0xff,							
		0xff,							
		0xff,							
		0x7f,							
		0xff,							
		0xdf,							
		0x5f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x57,							
		0x57,							
		0x57,							
	},									
	{									
		0xff,	/* XXXXXXXXXXXXXXXX */	// 3
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	/* 192 */
		0xff,							
		0xff,							
		0x7f,							
		0x5f,							
		0x57,							
		0x57,							
		0x57,							
		0x7f,							
		0x5f,							
		0x57,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x57,							
		0x57,							
		0x57,							
	},									
	{									
		0xff,	/* XXXXXXXXXXXXXXXX */	// 4
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	/* 240 */
		0xff,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x57,							
		0x57,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x57,							
		0x57,							
		0x57,							
	},									
	{									
		0xff,	/* XXXXXXXXXXXXXXXX */	// 5
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xff,	/* XXXXXXXXXXXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xfd,	/* XXXXXX-X-XXXXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xf5,	/* XXXX-X-X-X-XXXXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	
		0xd5,	/* XX-X-X-X-X-X-XXX */	/* 288 */
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0xff,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x5f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x7f,							
		0x5f,							
		0x5f,							
		0x57,							
		0x57,							
	}									
};										
const int gfxTileSetSize = sizeof(gfxTiles) / sizeof(gfxTiles[0]);
