/*
 *	platRP6502.c
 *	cc65 Chess
 *
 *	Picocomputer 6502 port, 2026.
 *
 *	Video memory here is XRAM on the VGA co-processor, reached a byte at a time
 *	through RIA.addr0 / RIA.rw0 with an auto-increment.  None of it costs the
 *	6502 a byte, which is why this is the one target with no memory pressure -
 *	and why it can afford two character planes instead of one:
 *
 *	  plane 0  a custom font holding the 32x24 piece art sliced into 4x3 8x8
 *	           glyphs.  the board, and nothing else
 *	  plane 1  the built-in font.  labels, the move log, menus, messages, and
 *	           the numbers the B display lays over the squares
 *
 *	Palette entry 0 is the transparent one, so a plane 1 cell with background 0
 *	lets the board show through and a cell with any other background covers it.
 *	That is what makes a menu a menu and the B display an overlay, and it is
 *	why no ASCII font has to be shipped: only the pieces need a custom glyph.
 *
 *	There is no conio on this target - no cgetc, no kbhit, no gotoxy - so keys
 *	come from the RIA's HID key bitmap and every character on screen is put
 *	there by this file.
 */

#include <rp6502.h>
#include <stdio.h>
#include <string.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "dataRP6502.h"

/*-----------------------------------------------------------------------*/
#define BOARD_PIECE_WIDTH		4
#define BOARD_PIECE_HEIGHT		3
#define SCREEN_WIDTH			40
#define SCREEN_HEIGHT			30
#define LOG_WINDOW_LEFT			(2 + 8 * BOARD_PIECE_WIDTH)
#define LOG_WINDOW_BOTTOM		(8 * BOARD_PIECE_HEIGHT)
#define MESSAGE_ROW				(1 + 8 * BOARD_PIECE_HEIGHT)
#define SCROLL_SPEED			6

/*-----------------------------------------------------------------------*/
// XRAM.  All 64K of it is free, so nothing here is squeezed
#define XRAM_BOARD				0x0000	// 40*30 cells of glyph, fore, back
#define XRAM_TEXT				0x1000	// the same again, one plane up
#define XRAM_FONT				0x2000	// 256 glyphs of 8 rows
#define XRAM_BOARD_CFG			0xFE00
#define XRAM_TEXT_CFG			0xFE20
#define XRAM_KEYS				0xFF10	// 32 byte HID key bitmap

#define CELL(x, y)				(((y) * SCREEN_WIDTH + (x)) * 3)

// Glyph 0 is blank; the pieces follow, 12 glyphs each, in the order
// gfxTiles is written - piece, then solid/outline, then the cells in
// reading order
#define PIECE_CELLS				(BOARD_PIECE_WIDTH * BOARD_PIECE_HEIGHT)
#define GLYPH_PIECE(p, v)		(1 + (((p) - 1) * 2 + (v)) * PIECE_CELLS)

/*-----------------------------------------------------------------------*/
// Palette indices.  0-15 are the ANSI colours, 16 up are a 6x6x6 cube, and
// only entry 0 is transparent - entry 16 is an opaque black, which is what
// the dark squares want
#define COL_CLEAR				0
#define COL_LIGHT				15		// light board square
#define COL_DARK				16		// dark board square
#define COL_BACKDROP			2		// the green surround the other ports use
#define COL_TEXT				15
#define COL_LABEL				10		// the B display's counts, on a square
#define COL_COORD				11		// A..H and 1..8, on the green surround
#define COL_VALUE				13		// the B display's piece value
#define COL_MENU_FRAME			11
#define COL_MENU_BACK			4
#define COL_MENU_ITEM			7
#define COL_MENU_PICK			15
#define COL_SCROLLER			14
#define COL_ERROR				9

// The HCOLOR_* values in types.h are CBM colour codes.  This maps the six the
// game actually asks for onto the ANSI palette, keeping the meanings the
// readme documents: green selectable, red stuck, purple empty, blue selected,
// cyan reachable
static const char sc_hcolor[7] =
{
	COL_CLEAR,
	15,			// 1 white
	9,			// 2 red
	14,			// 3 cyan
	13,			// 4 purple
	10,			// 5 green
	12,			// 6 blue
};

/*-----------------------------------------------------------------------*/
// HID usage codes for the thirteen keys this game uses
#define KEY_A					0x04
#define KEY_B					0x05
#define KEY_D					0x07
#define KEY_M					0x10
#define KEY_R					0x15
#define KEY_U					0x18
#define KEY_ENTER				0x28
#define KEY_ESC					0x29
#define KEY_RIGHT				0x4F
#define KEY_LEFT				0x50
#define KEY_DOWN				0x51
#define KEY_UP					0x52

#define REPEAT_DELAY			24		// vsyncs before a held cursor key repeats
#define REPEAT_RATE				5		// and between repeats after that

/*-----------------------------------------------------------------------*/
// Internal function Prototype
char plat_TimeExpired(unsigned int aTime, char *timerInit);

/*-----------------------------------------------------------------------*/
// Local storage
char textStr[SCREEN_WIDTH + 1];			// All visible text goes through here

static char sc_held[32];				// previous key sample, for the edges
static char sc_anyHeld;					// is sc_held worth clearing
static char sc_repeat;					// usage code being repeated, 0 for none
static char sc_repeatAt;				// vsync at which it fires again

/*-----------------------------------------------------------------------*/
// Fill a run of cells on either plane.  Cells are three bytes and rows are
// contiguous, so a run inside one row is one address and a stream of writes
static void plat_fillCells(unsigned addr, char count, char glyph, char fore, char back)
{
	RIA.addr0 = addr;
	RIA.step0 = 1;
	while(count--)
	{
		RIA.rw0 = glyph;
		RIA.rw0 = fore;
		RIA.rw0 = back;
	}
}

/*-----------------------------------------------------------------------*/
// Put a string on the text plane.  A background of COL_CLEAR leaves the board
// showing through, which is what everything except a menu wants
static void plat_showStrXY(char fore, char back, char x, char y, char *str)
{
	RIA.addr0 = XRAM_TEXT + CELL(x, y);
	RIA.step0 = 1;
	while(*str && x++ < SCREEN_WIDTH)
	{
		RIA.rw0 = *str++;
		RIA.rw0 = fore;
		RIA.rw0 = back;
	}
}

/*-----------------------------------------------------------------------*/
// Erase a rectangle of the text plane back to transparent
static void plat_clearText(char x, char y, char w, char h)
{
	while(h--)
		plat_fillCells(XRAM_TEXT + CELL(x, y++), w, ' ', COL_TEXT, COL_CLEAR);
}

/*-----------------------------------------------------------------------*/
// Put the 4x3 cells of a piece on the board plane, or 4x3 empty cells when
// glyph is 0.  This is the whole of drawing a square: twelve cells, thirty
// six bytes
static void plat_showPiece(char x, char y, char glyph, char fore, char back)
{
	char row, col, g = glyph;

	for(row = 0; row < BOARD_PIECE_HEIGHT; ++row)
	{
		RIA.addr0 = XRAM_BOARD + CELL(x, y + row);
		RIA.step0 = 1;
		for(col = 0; col < BOARD_PIECE_WIDTH; ++col)
		{
			RIA.rw0 = glyph ? g++ : 0;
			RIA.rw0 = fore;
			RIA.rw0 = back;
		}
	}
}

/*-----------------------------------------------------------------------*/
// Slice the 32x24 piece art into 8x8 glyphs and send it up.  The font is
// stored a row at a time across all 256 glyphs - row 0 of every glyph, then
// row 1, and so on - so walking it in that order makes the whole upload eight
// runs of sequential writes instead of 2048 addressed ones
static void plat_loadFont(void)
{
	char row, piece, index, cy, cx;
	unsigned g;

	for(row = 0; row < 8; ++row)
	{
		RIA.addr0 = XRAM_FONT + row * 256;
		RIA.step0 = 1;

		RIA.rw0 = 0;					// glyph 0, the empty square

		for(piece = 0; piece < PAWN; ++piece)
			for(index = 0; index < 2; ++index)
				for(cy = 0; cy < BOARD_PIECE_HEIGHT; ++cy)
					for(cx = 0; cx < BOARD_PIECE_WIDTH; ++cx)
						RIA.rw0 = gfxTiles[piece][index][(cy * 8 + row) * BOARD_PIECE_WIDTH + cx];

		for(g = 1 + PAWN * 2 * PIECE_CELLS; g < 256; ++g)
			RIA.rw0 = 0;
	}
}

/*-----------------------------------------------------------------------*/
// Both planes are the same shape and differ only in where their cells and
// their font live
static void plat_configPlane(unsigned cfg, unsigned data, unsigned font)
{
	xram0_struct_set(cfg, vga_mode1_config_t, x_wrap, 0);
	xram0_struct_set(cfg, vga_mode1_config_t, y_wrap, 0);
	xram0_struct_set(cfg, vga_mode1_config_t, x_pos_px, 0);
	xram0_struct_set(cfg, vga_mode1_config_t, y_pos_px, 0);
	xram0_struct_set(cfg, vga_mode1_config_t, width_chars, SCREEN_WIDTH);
	xram0_struct_set(cfg, vga_mode1_config_t, height_chars, SCREEN_HEIGHT);
	xram0_struct_set(cfg, vga_mode1_config_t, xram_data_ptr, data);
	xram0_struct_set(cfg, vga_mode1_config_t, xram_palette_ptr, 0xFFFF);
	xram0_struct_set(cfg, vga_mode1_config_t, xram_font_ptr, font);
}

/*-----------------------------------------------------------------------*/
// Called one-time to set up the platform (or computer or whatever)
void plat_Init()
{
	char i;

	// Setting this to 0 will not show the "Quit" option in the main menu.
	// exit() drops back to the monitor cleanly here, so the option is real
	gReturnToOS = 1;

	// 320x240 is 40x30 cells of the 8x8 font, which puts the same 40 column
	// layout the other ports use on the screen with five rows to spare
	xreg_vga_canvas(1);

	plat_loadFont();
	plat_configPlane(XRAM_BOARD_CFG, XRAM_BOARD, XRAM_FONT);
	plat_configPlane(XRAM_TEXT_CFG, XRAM_TEXT, 0xFFFF);

	// mode 1, 8 bits of colour - three bytes a cell, glyph then fore then back
	xreg_vga_mode(1, 3, XRAM_BOARD_CFG, 0);
	xreg_vga_mode(1, 3, XRAM_TEXT_CFG, 1);

	for(i = 0; i < SCREEN_HEIGHT; ++i)
	{
		plat_fillCells(XRAM_BOARD + CELL(0, i), SCREEN_WIDTH, 0, COL_TEXT, COL_BACKDROP);
		plat_fillCells(XRAM_TEXT + CELL(0, i), SCREEN_WIDTH, ' ', COL_TEXT, COL_CLEAR);
	}

	xreg_ria_keyboard(XRAM_KEYS);

	// Prime the key state from whatever is held right now.  The Enter that
	// typed LOAD is usually still down when this runs, and without this it
	// reads as a press and skips the title
	RIA.addr0 = XRAM_KEYS;
	RIA.step0 = 1;
	for(i = 0; i < 32; ++i)
		sc_held[i] = RIA.rw0;
	sc_anyHeld = 1;

	// Show the welcome text
	strcpy(textStr, "Picocomputer 6502 version, 2026.");
	plat_showStrXY(COL_MENU_FRAME, COL_CLEAR, 2, SCREEN_HEIGHT / 2 - 1, gszAbout);
	plat_showStrXY(COL_MENU_FRAME, COL_CLEAR, 4, SCREEN_HEIGHT / 2 + 1, textStr);

	// Show the welcome kings (black and white, solid versions)
	plat_showPiece(SCREEN_WIDTH / 2 - 2, SCREEN_HEIGHT / 2 - 7, GLYPH_PIECE(KING, 1), COL_DARK, COL_BACKDROP);
	plat_showPiece(SCREEN_WIDTH / 2 - 2, SCREEN_HEIGHT / 2 + 3, GLYPH_PIECE(KING, 1), COL_LIGHT, COL_BACKDROP);

	plat_ReadKeys(1);
}

/*-----------------------------------------------------------------------*/
// The VGA reads XRAM itself, so there is nothing to push
void plat_UpdateScreen()
{
}

/*-----------------------------------------------------------------------*/
// Very simple menu with a heading and a scrolling banner as a footer
char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller, *pEnd;
	int keyMask;
	char i, sx, sy, numMenuItems, timerInit = 0, maxLen = 0;

	// If the scroller message chages, cache the new one
	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
		pEnd = scroller + strlen(scroller);
	}

	// Find the longest entry
	for(numMenuItems = 0; menuItems[numMenuItems]; ++numMenuItems)
	{
		char len = strlen(menuItems[numMenuItems]);
		if(len > maxLen)
			maxLen = len;
	}

	// Centre on the screen
	sy = MAX_SIZE(0, (SCREEN_HEIGHT / 2) - (height / 2) - 1);
	sx = MAX_SIZE(0, (SCREEN_WIDTH / 2) - (maxLen / 2) - 1);
	maxLen = MIN_SIZE(SCREEN_WIDTH - 2, maxLen);

	// Draw a frame for the menu.  These cells are opaque, so the board goes
	// away underneath them and comes back when the menu is redrawn over
	sprintf(textStr, "%-*s", maxLen + 4, " ");
	plat_showStrXY(COL_MENU_FRAME, COL_MENU_FRAME, sx - 1, sy - 1, textStr);
	plat_showStrXY(COL_MENU_FRAME, COL_MENU_FRAME, sx - 1, sy + height + 2, textStr);
	*(textStr + 1) = '\0';
	for(i = 0; i < height + 2; i++)
	{
		plat_showStrXY(COL_MENU_FRAME, COL_MENU_FRAME, sx - 1, sy + i, textStr);
		plat_showStrXY(COL_MENU_FRAME, COL_MENU_FRAME, sx + maxLen + 2, sy + i, textStr);
	}

	// Show the title
	sprintf(textStr, " %.*s ", SCREEN_WIDTH - 2, menuItems[0]);
	plat_showStrXY(COL_MENU_FRAME, COL_MENU_BACK, sx, sy, textStr);

	// Leave a blank line
	sprintf(textStr, "%-*s", maxLen + 2, " ");
	plat_showStrXY(COL_MENU_ITEM, COL_MENU_BACK, sx, ++sy, textStr);

	// Show all the menu items
	for(i = 1; i < numMenuItems; ++i)
	{
		sprintf(textStr, " %.*s ", maxLen, menuItems[i]);
		plat_showStrXY(COL_MENU_ITEM, COL_MENU_BACK, sx, sy + i, textStr);
	}

	// Pad with blank lines to menu height
	for(; i < height; ++i)
	{
		sprintf(textStr, "%-*s", maxLen + 2, " ");
		plat_showStrXY(COL_MENU_ITEM, COL_MENU_BACK, sx, sy + i, textStr);
	}

	// Select the first item
	i = 1;
	do
	{
		// Highlight the selected item
		sprintf(textStr, ">%.*s<", maxLen, menuItems[i]);
		plat_showStrXY(COL_MENU_PICK, COL_MENU_BACK, sx, sy + i, textStr);

		// Look for user input
		keyMask = plat_ReadKeys(0);

		if(keyMask & INPUT_MOTION)
		{
			// selection changes so de-highlight the selected item
			sprintf(textStr, " %.*s ", maxLen, menuItems[i]);
			plat_showStrXY(COL_MENU_ITEM, COL_MENU_BACK, sx, sy + i, textStr);

			// see if the selection goes up or down
			switch(keyMask & INPUT_MOTION)
			{
				case INPUT_UP:
					if(!--i)
						i = numMenuItems - 1;
				break;

				case INPUT_DOWN:
					if(numMenuItems == ++i)
						i = 1;
				break;
			}
		}
		keyMask &= (INPUT_SELECT | INPUT_BACKUP);

		// Show the scroller
		sprintf(textStr, " %.*s ", maxLen, pScroller);
		plat_showStrXY(COL_SCROLLER, COL_MENU_BACK, sx, sy + height, textStr);

		// Wrap the message if needed
		if((pEnd - pScroller) < maxLen - 1)
		{
			sprintf(textStr, " %.*s ", maxLen - (pEnd - pScroller) - 1, scroller);
			plat_showStrXY(COL_SCROLLER, COL_MENU_BACK, sx + (pEnd - pScroller) + 1, sy + height, textStr);
		}

		// Only update the scrolling when needed
		if(plat_TimeExpired(SCROLL_SPEED, &timerInit))
		{
			++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
	} while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);

	// if backing out of the menu, return 0
	if(keyMask & INPUT_BACKUP)
		return 0;

	// return the selection
	return i;
}

/*-----------------------------------------------------------------------*/
// Draw the chess board and possibly clear the log section
void plat_DrawBoard(char clearLog)
{
	char i;

	// only the board columns get wiped.  a full width clear flashes the log
	// on undo - LogMove has just drawn the new lines and this would take them
	// away and put them back
	plat_clearText(0, 0, LOG_WINDOW_LEFT - 1, SCREEN_HEIGHT);

	if(clearLog)
		plat_clearText(LOG_WINDOW_LEFT - 1, 0, SCREEN_WIDTH - LOG_WINDOW_LEFT + 1, SCREEN_HEIGHT);

	// Add the A..H and 1..8 tile-keys.  The other ports draw these green; here
	// green is the surround they would be sitting on
	for(i = 0; i < 8; ++i)
	{
		sprintf(textStr, "%c", 'A' + i);
		plat_showStrXY(COL_COORD, COL_CLEAR, 3 + i * BOARD_PIECE_WIDTH, 0, textStr);
		sprintf(textStr, "%d", 8 - i);
		plat_showStrXY(COL_COORD, COL_CLEAR, 0, 2 + i * BOARD_PIECE_HEIGHT, textStr);
	}

	// redraw all tiles
	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);
}

/*-----------------------------------------------------------------------*/
// Draw a tile with background and piece on it for positions 0..63
void plat_DrawSquare(char position)
{
	char index, piece, color, glyph = 0;
	char y = position / 8, x = position & 7;
	char blackWhite = !((x & 1) ^ (y & 1));
	char back = blackWhite ? COL_LIGHT : COL_DARK;
	char fore = blackWhite ? COL_DARK : COL_LIGHT;

	// Get the piece data to draw the piece over the tile
	piece = gChessBoard[y][x];
	color = piece & PIECE_WHITE;
	piece &= PIECE_DATA;

	// The piece is always drawn in the square's opposite colour, so a piece
	// that matches the square it stands on gets the outlined art instead of
	// the solid.  That is how the two are told apart on a two colour square,
	// and it is the same rule every other port follows
	if(piece)
	{
		index = 1;
		if((color && blackWhite) || (!color && !blackWhite))
			index = 0;
		glyph = GLYPH_PIECE(piece, index);
	}

	plat_showPiece(1 + x * BOARD_PIECE_WIDTH, 1 + y * BOARD_PIECE_HEIGHT, glyph, fore, back);

	// Show the attack numbers.  These go on the text plane over the piece,
	// transparent so the piece stays visible behind them
	if(gShowAttackBoard)
	{
		char piece_value = (gChessBoard[y][x] & 0x0f);
		char piece_color = (gChessBoard[y][x] & PIECE_WHITE) >> 7;

		// Attackers (bottom left)
		sprintf(textStr, "%d", (gpAttackBoard[giAttackBoardOffset[position][0]]));
		plat_showStrXY(piece_color ? COL_ERROR : COL_LABEL, COL_CLEAR,
		               1 + x * BOARD_PIECE_WIDTH, (y + 1) * BOARD_PIECE_HEIGHT, textStr);

		// Defenders (bottom right)
		sprintf(textStr, "%d", (gpAttackBoard[giAttackBoardOffset[position][1]]));
		plat_showStrXY(!piece_color ? COL_ERROR : COL_LABEL, COL_CLEAR,
		               1 + x * BOARD_PIECE_WIDTH + 3, (y + 1) * BOARD_PIECE_HEIGHT, textStr);

		// Piece value top left (1=ROOK, 2=KNIGHT, 3=BISHOP, 4=QUEEN, 5=KING, 6=PAWN)
		sprintf(textStr, "%0d", piece_value);
		plat_showStrXY(COL_VALUE, COL_CLEAR, 1 + x * BOARD_PIECE_WIDTH, 1 + y * BOARD_PIECE_HEIGHT, textStr);

		// Colour top right (0 is black, 1 is white)
		sprintf(textStr, "%d", piece_color);
		plat_showStrXY(piece_color ? COL_LIGHT : 8, COL_CLEAR,
		               1 + x * BOARD_PIECE_WIDTH + 3, 1 + y * BOARD_PIECE_HEIGHT, textStr);
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	// white and black, literally - both readable against the green surround,
	// where the grey the other ports use for black would not be
	sprintf(textStr, "%s", gszSideLabel[side]);
	plat_showStrXY(side ? COL_LIGHT : COL_DARK, COL_CLEAR, LOG_WINDOW_LEFT, 0, textStr);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char)
{
	char row;
	char y = 1 + (position / 8) * BOARD_PIECE_HEIGHT;
	char x = 1 + (position & 7) * BOARD_PIECE_WIDTH;
	char c = sc_hcolor[color & 7];

	// Recolour the background of the square's left and right columns - the
	// same two vertical bars the C64 and the CX16 draw.  Only the third byte
	// of a cell moves, so step over whole rows
	RIA.step0 = SCREEN_WIDTH * 3;

	RIA.addr0 = XRAM_BOARD + CELL(x, y) + 2;
	for(row = 0; row < BOARD_PIECE_HEIGHT; ++row)
		RIA.rw0 = c;

	RIA.addr0 = XRAM_BOARD + CELL(x + BOARD_PIECE_WIDTH - 1, y) + 2;
	for(row = 0; row < BOARD_PIECE_HEIGHT; ++row)
		RIA.rw0 = c;
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char color)
{
	// The five rows the other ports do not have go to the message, so it gets
	// the width of the screen instead of the width of the log column
	sprintf(textStr, "%-*.*s", SCREEN_WIDTH - 2, SCREEN_WIDTH - 2, str);
	plat_showStrXY(sc_hcolor[color & 7], COL_CLEAR, 1, MESSAGE_ROW, textStr);
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage()
{
	plat_clearText(1, MESSAGE_ROW, SCREEN_WIDTH - 2, 1);
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWin()
{
	char bot = LOG_WINDOW_BOTTOM - 2, y = 1, x = LOG_WINDOW_LEFT;

	// walk the undo stack.  gTile is only filled by undo_FindUndoLine now, so
	// the old scroll-and-print-current-globals path logged NULL_TILE as
	// "A(-A(" and then repeated the previous move
	for(; y <= bot; ++y)
	{
		if(undo_FindUndoLine(bot - y))
		{
			frontend_FormatLogString();
			sprintf(textStr, "%-6s", gLogStrBuffer);
			plat_showStrXY(gColor[0] ? COL_LIGHT : COL_DARK, COL_CLEAR, x, y, textStr);
		}
		else
			plat_clearText(x, y, SCREEN_WIDTH - x, 1);
	}
}

/*-----------------------------------------------------------------------*/
// Important note about this function is that it alters the gTile...
// global data trackers so beware when calling it
void plat_AddToLogWinTop()
{
	// This redraws the whole log window so just call it
	plat_AddToLogWin();
}

/*-----------------------------------------------------------------------*/
// interval in vsyncs.  RIA.vsync ticks at 60Hz whenever the VGA is attached,
// which it is - this file put a mode on it
char plat_TimeExpired(unsigned int aTime, char *timerInit)
{
	char now = RIA.vsync;
	char last = *timerInit;

	if(!last || (char)(now - last) >= (char)aTime)
	{
		*timerInit = now ? now : 1;
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static int plat_KeyMask(char code)
{
	switch(code)
	{
		case KEY_UP:		return INPUT_UP;
		case KEY_RIGHT:		return INPUT_RIGHT;
		case KEY_DOWN:		return INPUT_DOWN;
		case KEY_LEFT:		return INPUT_LEFT;
		case KEY_ESC:		return INPUT_BACKUP;	// this machine's RUN/STOP
		case KEY_A:			return INPUT_TOGGLE_A;
		case KEY_B:			return INPUT_TOGGLE_B;
		case KEY_D:			return INPUT_TOGGLE_D;
		case KEY_M:			return INPUT_MENU;
		case KEY_ENTER:		return INPUT_SELECT;
		case KEY_R:			return INPUT_REDO;
		case KEY_U:			return INPUT_UNDO;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// The RIA keeps a 256 bit map of held HID keys in XRAM: bit N is set while the
// key with usage code N is down, and bit 0 of byte 0 is set while nothing is
// down at all.  That last bit is what makes this affordable - the search asks
// this question every 64 nodes, and the usual answer costs four stores, a load
// and a branch.
//
// It is level state and not a queue, so the edge detection and the auto-repeat
// that cgetc hands every other port for free have to be done here.
int plat_ReadKeys(char blocking)
{
	static char now[32];
	char i, b, fresh, code;
	int mask;

	for(;;)
	{
		RIA.addr0 = XRAM_KEYS;
		RIA.step0 = 1;
		now[0] = RIA.rw0;

		if(now[0] & 1)
		{
			// nothing down.  forget the edges so the next press is one, and
			// do it once rather than on every poll
			if(sc_anyHeld)
			{
				memset(sc_held, 0, sizeof(sc_held));
				sc_anyHeld = 0;
				sc_repeat = 0;
			}
			if(!blocking)
				return 0;
			continue;
		}

		for(i = 1; i < 32; ++i)
			now[i] = RIA.rw0;
		sc_anyHeld = 1;

		// the first key that is down now and was not down before
		code = 0;
		for(i = 0; i < 32; ++i)
		{
			fresh = now[i] & ~sc_held[i];
			if(!i)
				fresh &= ~0x0f;			// bit 0 is "no key", 1-3 are the lock LEDs
			sc_held[i] = now[i];
			if(fresh && !code)
				for(b = 0; b < 8; ++b)
					if(fresh & SET_BIT(b))
					{
						code = (i << 3) | b;
						break;
					}
		}

		if(code)
		{
			mask = plat_KeyMask(code);
			// only the cursor keys repeat.  a held M would open the menu,
			// close it, and open it again
			sc_repeat = (mask & INPUT_MOTION) ? code : 0;
			sc_repeatAt = RIA.vsync + REPEAT_DELAY;
			if(mask)
				return mask;
		}
		else if(sc_repeat && (char)(RIA.vsync - sc_repeatAt) < 128)
		{
			// nothing new, but a held cursor key is due again
			sc_repeatAt = RIA.vsync + REPEAT_RATE;
			return plat_KeyMask(sc_repeat);
		}

		if(!blocking)
			return 0;
	}
}

/*-----------------------------------------------------------------------*/
char plat_GetSeed()
{
	// RIA.vsync at $FFE3 counts frames at 60Hz and is the free running counter
	// this machine offers.  There is a true RNG behind RIA_ATTR_LRAND, but
	// that is an OS call where the bar for this function is a register read -
	// and by the time it is asked the human has walked the menu, so the frame
	// count is already unrepeatable
	return RIA.vsync;
}

/*-----------------------------------------------------------------------*/
// Only gets called if gReturnToOS is true, which it is
void plat_Shutdown()
{
	// hand the screen back to the console before exit() returns to the monitor
	xreg_vga_canvas(0);
	printf("\n");
}
