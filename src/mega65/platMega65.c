/* native MEGA65, 45GS02 code and VIC-IV fast mode.  the C64 layout and
 * artwork are retained; rendering is C, with no cc65 runtime dependency. */

#include <string.h>
#include <stdint.h>
#include <mega65.h>
#include <cbm.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"

extern const char gfxTiles[PAWN][96];

/*-----------------------------------------------------------------------*/
#define SCREEN_WIDTH			40
#define SCREEN_HEIGHT			25
#define BOARD_X					1
#define BOARD_Y					1
#define BOARD_PIECE_WIDTH		4
#define BOARD_PIECE_HEIGHT		3
#define LOG_X					33
#define LOG_W					7
#define SCROLL_CS				32

#define COL_FIELD				COLOR_GREEN
#define COL_DARK				COLOR_GREEN
#define COL_LIGHT				COLOR_LIGHTGREEN
#define COL_BLACK				COLOR_BLACK
#define COL_WHITE				COLOR_WHITE
#define COL_YELLOW				COLOR_YELLOW
#define COL_BLUE				COLOR_BLUE
#define COL_CYAN				COLOR_CYAN
#define COL_RED					COLOR_RED
#define COL_MAGENTA				COLOR_PURPLE
#define COL_GRAY				COLOR_LIGHTGREY

/* HCOLOR_* as bar colours.  C64 uses the VIC numbers (empty is purple,
 * valid is green, attack is cyan).  green bars vanish on the dark
 * squares, so empty is magenta and valid/attack use bright cyan. */
static const char sc_hcol[] =
{
	COL_BLACK,
	COL_WHITE,
	COL_RED,		/* invalid / black */
	COLOR_CYAN,		/* attack — destination */
	COLOR_PURPLE,		/* empty / enemy */
	COLOR_CYAN,		/* valid — own piece with moves */
	COL_YELLOW,		/* selected */
	COL_BLUE
};

static char textStr[41];
extern const char sc_font[96][8];
#define BITMAP ((char *)0xa000)
#define MATRIX ((char *)0xc000)
static char saved[0x80];
static char scrollFrames;
static char savedPalette[3][16];
/* C64 Pepto PAL palette, as shipped with VICE. */
static const char colours[3][16] =
{
	{0x00,0xff,0x68,0x70,0x6f,0x58,0x35,0xb8,0x6f,0x43,0x9a,0x44,0x6c,0x9a,0x6c,0x95},
	{0x00,0xff,0x37,0xa4,0x3d,0x8d,0x28,0xc7,0x4f,0x39,0x67,0x44,0x6c,0xd2,0x5e,0x95},
	{0x00,0xff,0x2b,0xb2,0x86,0x43,0x79,0x6f,0x25,0x00,0x59,0x44,0x6c,0x84,0xb5,0x95}
};

static void waitvblank(void)
{
	while(VICIV.rasterline == 255) {}
	while(VICIV.rasterline != 255) {}
}

static void fill_rect(int x, int y, int w, int h, unsigned char colour)
{
	char cx, cy;
	int off;
	for(cy = y / 8; cy < (y + h) / 8; ++cy)
		for(cx = x / 8; cx < (x + w) / 8; ++cx)
		{
			off = (int)cy * 40 + cx;
			memset(BITMAP + off * 8, 0, 8);
			MATRIX[off] = colour;
		}
}

static void putch_xy(char x, char y, char ch, unsigned char fg, unsigned char bg)
{
	int off;
	if(ch < 32 || ch > 127)
		ch = ' ';
	off = (int)y * 40 + x;
	memcpy(BITMAP + off * 8, sc_font[ch - 32], 8);
	MATRIX[off] = (fg << 4) | bg;
}

static void put_span(char x, char y, char *s, char n, unsigned char fg, unsigned char bg)
{
	char i;
	for(i = 0; i < n; ++i)
	{
		putch_xy(x + i, y, *s ? *s : ' ', fg, bg);
		if(*s) ++s;
	}
}

static char digit_or_hex(char n)
{
	if(n < 10)
		return (char)('0' + n);
	return (char)('A' + (n - 10));
}

static void copy_pad(char *dst, char *src, char width, char lead, char trail)
{
	char i;
	char room;

	i = 0;
	if(lead)
		dst[i++] = lead;
	room = (char)(width - (trail ? 1 : 0));
	while(*src && i < room)
		dst[i++] = *src++;
	while(i < room)
		dst[i++] = ' ';
	if(trail)
		dst[i++] = trail;
	dst[i] = 0;
}

static void fill_scroll(char *dst, char *p, char *start, char n)
{
	char i;

	i = 0;
	dst[i++] = ' ';
	while(i < n - 1)
	{
		if(!*p)
			p = start;
		if(!*p)
			break;
		dst[i++] = *p++;
	}
	while(i < n)
		dst[i++] = ' ';
	dst[n] = 0;
}

static void puts_centered(char y, char *s, unsigned char fg, unsigned char bg)
{
	char n;

	n = (char)strlen(s);
	while(n && s[n - 1] == ' ')
		--n;
	put_span((char)((SCREEN_WIDTH - n) / 2), y, s, n, fg, bg);
}

static void blit_piece(char sx, char sy, char piece, char white)
{
	char x, y;
	int off;
	if(!piece) return;
	for(y = 0; y < 3; ++y)
		for(x = 0; x < 4; ++x)
		{
			off = (int)(sy + y) * 40 + sx + x;
			memcpy(BITMAP + off * 8, gfxTiles[piece - 1] + y * 32 + x * 8, 8);
			MATRIX[off] = (MATRIX[off] & 15) | ((white ? COLOR_WHITE : COLOR_BLACK) << 4);
		}
}

static char plat_TimeExpired(unsigned int cs)
{
	(void)cs;
	if(++scrollFrames >= 16)
	{
		scrollFrames = 0;
		return 1;
	}
	return 0;
}

static int scan_keys(void)
{
	char k;
	k = cbm_k_getin();
	switch(k)
	{
		case 145: return INPUT_UP;
		case 17: return INPUT_DOWN;
		case 157: return INPUT_LEFT;
		case 29: return INPUT_RIGHT;
		case 13: return INPUT_SELECT;
		case 3: case 27: return INPUT_BACKUP;
		case 'A': case 'a': return INPUT_TOGGLE_A;
		case 'B': case 'b': return INPUT_TOGGLE_B;
		case 'D': case 'd': return INPUT_TOGGLE_D;
		case 'M': case 'm': return INPUT_MENU;
		case 'U': case 'u': return INPUT_UNDO;
		case 'R': case 'r': return INPUT_REDO;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
	char i, channel, v;
	volatile char *palette;
	gReturnToOS = 1;
	VICIV.key = 0x47;
	VICIV.key = 0x53;
	memcpy(saved, (const void *)0xd000, sizeof(saved));
	VICIV.ctrl1 &= ~16;
	VICIV.palsel = 0;
	for(channel = 0; channel < 3; ++channel)
	{
		palette = (volatile char *)(0xd100 + (unsigned int)channel * 256);
		for(i = 0; i < 16; ++i)
		{
			savedPalette[channel][i] = palette[i];
			v = colours[channel][i];
			palette[i] = (v << 4) | (v >> 4);
		}
	}
	VICIV.spr_ena = 0;
	/* hot registers establish 320x200 geometry, then precise native pointers
	 * select reserved RAM independently of the legacy VIC bank. */
	VICIV.sdbdrwd_msb |= VIC4_HOTREG_MASK;
	VICIV.ctrlb = 0x40;
	VICIV.ctrlc = VIC4_VFAST_MASK;
	VICIV.ctrl2 = 0x08;
	VICIV.ctrl1 = 0x2b;
	VICIV.sdbdrwd_msb &= ~VIC4_HOTREG_MASK;
	VICIV.scrnptr_lsb = 0;
	VICIV.scrnptr_msb = 0xc0;
	VICIV.scrnptr_bnk = 0;
	VICIV.scrnptr_mb &= 0xf0;
	VICIV.charptr_lsb = 0;
	VICIV.charptr_msb = 0xa0;
	VICIV.charptr_bnk = 0;
	VICIV.linestep = 40;
	VICIV.chrcount = 40;
	VICIV.bordercol = COLOR_GREEN;
	fill_rect(0, 0, 320, 200, COL_FIELD);
	VICIV.ctrl1 |= 16;

	puts_centered(SCREEN_HEIGHT / 2 - 1, gszAbout, COL_WHITE, COL_FIELD);
	puts_centered(SCREEN_HEIGHT / 2 + 1, (char *)"MEGA65 native, 2026.", COL_CYAN, COL_FIELD);
	fill_rect((SCREEN_WIDTH / 2 - 2) << 3, (SCREEN_HEIGHT / 2 - 6) << 3,
		32, 24, COL_FIELD);
	blit_piece((char)(SCREEN_WIDTH / 2 - 2), (char)(SCREEN_HEIGHT / 2 - 6), KING, 0);
	fill_rect((SCREEN_WIDTH / 2 - 2) << 3, (SCREEN_HEIGHT / 2 + 4) << 3,
		32, 24, COL_FIELD);
	blit_piece((char)(SCREEN_WIDTH / 2 - 2), (char)(SCREEN_HEIGHT / 2 + 4), KING, 1);
	puts_centered(22, (char *)"Press ENTER", COL_YELLOW, COL_FIELD);

	plat_ReadKeys(1);
}

void plat_UpdateScreen(void)
{
}

char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller;
	int keyMask;
	char i, sx, sy, numMenuItems, maxLen;

	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
	}

	maxLen = 0;
	for(numMenuItems = 0; menuItems[numMenuItems]; ++numMenuItems)
	{
		char len = (char)strlen(menuItems[numMenuItems]);
		if(len > maxLen)
			maxLen = len;
	}

	sy = (char)MAX_SIZE(0, (SCREEN_HEIGHT / 2) - (height / 2) - 1);
	sx = (char)MAX_SIZE(0, (SCREEN_WIDTH / 2) - (maxLen / 2) - 1);
	maxLen = (char)MIN_SIZE(SCREEN_WIDTH - 2, maxLen);

	fill_rect((int)sx << 3, (int)sy << 3,
		((int)maxLen + 2) << 3, ((int)height + 2) << 3, COL_BLUE);
	fill_rect(((int)sx - 1) << 3, ((int)sy - 1) << 3,
		((int)maxLen + 4) << 3, 8, COL_YELLOW);
	fill_rect(((int)sx - 1) << 3, (int)sy << 3,
		8, ((int)height + 3) << 3, COL_YELLOW);
	fill_rect(((int)sx + maxLen + 2) << 3, (int)sy << 3,
		8, ((int)height + 3) << 3, COL_YELLOW);
	fill_rect((int)sx << 3, ((int)sy + height + 2) << 3,
		((int)maxLen + 2) << 3, 8, COL_YELLOW);

	copy_pad(textStr, menuItems[0], (char)(maxLen + 2), ' ', 0);
	put_span(sx, sy, textStr, (char)(maxLen + 2), COL_YELLOW, COL_BLUE);

	copy_pad(textStr, (char *)"", (char)(maxLen + 2), 0, 0);
	put_span(sx, ++sy, textStr, (char)(maxLen + 2), COL_GRAY, COL_BLUE);

	for(i = 1; i < numMenuItems; ++i)
	{
		copy_pad(textStr, menuItems[i], (char)(maxLen + 2), ' ', 0);
		put_span(sx, (char)(sy + i), textStr, (char)(maxLen + 2), COL_GRAY, COL_BLUE);
	}
	for(; i < height; ++i)
	{
		copy_pad(textStr, (char *)"", (char)(maxLen + 2), 0, 0);
		put_span(sx, (char)(sy + i), textStr, (char)(maxLen + 2), COL_GRAY, COL_BLUE);
	}

	i = 1;
	do
	{
		copy_pad(textStr, menuItems[i], (char)(maxLen + 2), '>', '<');
		put_span(sx, (char)(sy + i), textStr, (char)(maxLen + 2), COL_WHITE, COL_BLUE);

		keyMask = plat_ReadKeys(0);
		if(keyMask & INPUT_MOTION)
		{
			copy_pad(textStr, menuItems[i], (char)(maxLen + 2), ' ', 0);
			put_span(sx, (char)(sy + i), textStr, (char)(maxLen + 2), COL_GRAY, COL_BLUE);
			switch(keyMask & INPUT_MOTION)
			{
				case INPUT_UP:
					if(!--i)
						i = (char)(numMenuItems - 1);
					break;
				case INPUT_DOWN:
					if(numMenuItems == ++i)
						i = 1;
					break;
			}
		}
		keyMask &= (INPUT_SELECT | INPUT_BACKUP);

		fill_scroll(textStr, pScroller, scroller, (char)(maxLen + 2));
		put_span(sx, (char)(sy + height), textStr, (char)(maxLen + 2), COL_CYAN, COL_BLUE);

		if(plat_TimeExpired(SCROLL_CS))
		{
			++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
		waitvblank();
	} while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);

	if(keyMask & INPUT_BACKUP)
		return 0;
	return i;
}

void plat_DrawBoard(char clearLog)
{
	char i;

	fill_rect(0, 0, (1 + 8 * BOARD_PIECE_WIDTH) << 3, 8, COL_FIELD);
	fill_rect(0, 0, 8, SCREEN_HEIGHT << 3, COL_FIELD);
	if(clearLog)
		fill_rect(LOG_X << 3, 0, LOG_W << 3, SCREEN_HEIGHT << 3, COL_FIELD);

	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);

	for(i = 0; i < 8; ++i)
	{
		putch_xy((char)(BOARD_X + i * BOARD_PIECE_WIDTH + 1), 0,
			(char)('A' + i), COL_BLACK, COL_FIELD);
		putch_xy(0, (char)(BOARD_Y + i * BOARD_PIECE_HEIGHT + 1),
			(char)('8' - i), COL_BLACK, COL_FIELD);
	}
	if(!clearLog)
		plat_AddToLogWin();
}

void plat_DrawSquare(char position)
{
	char y;
	char x;
	char sx;
	char sy;
	char piece;
	char colour;
	char light;
	unsigned char paper;
	int px;
	int py;

	y = position / 8;
	x = position & 7;
	light = !((x & 1) ^ (y & 1));
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);
	paper = light ? COL_LIGHT : COL_DARK;
	px = (int)sx << 3;
	py = (int)sy << 3;

	fill_rect(px, py, BOARD_PIECE_WIDTH << 3, BOARD_PIECE_HEIGHT << 3, paper);

	piece = gChessBoard[y][x];
	colour = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	if(piece)
		blit_piece(sx, sy, piece, colour ? 1 : 0);

	if(gShowAttackBoard)
	{
		putch_xy(sx, (char)(sy + 2),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][0]]),
			COL_RED, paper);
		putch_xy((char)(sx + 3), (char)(sy + 2),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][1]]),
			COL_CYAN, paper);
		putch_xy(sx, sy,
			digit_or_hex((char)(gChessBoard[y][x] & 0x0F)),
			COL_MAGENTA, paper);
		putch_xy((char)(sx + 3), sy,
			digit_or_hex((char)(colour >> 7)),
			COL_YELLOW, paper);
	}
}

void plat_ShowSideToGoLabel(char side)
{
	unsigned char fg;

	fg = side ? COL_WHITE : COL_BLACK;
	put_span(LOG_X, 0, gszSideLabel[side], LOG_W, fg, COL_FIELD);
}

void plat_Highlight(char position, char color, char cursor)
{
	char x, y, row, column, bar, fg;
	int off;
	(void)cursor;
	x = BOARD_X + (position & 7) * BOARD_PIECE_WIDTH;
	y = BOARD_Y + (position / 8) * BOARD_PIECE_HEIGHT;
	bar = sc_hcol[color & 7];
	/* colour the side cells without erasing piece pixels or B's numbers. */
	for(row = 0; row < BOARD_PIECE_HEIGHT; ++row)
		for(column = 0; column < 2; ++column)
		{
			off = (int)(y + row) * 40 + x + column * (BOARD_PIECE_WIDTH - 1);
			fg = MATRIX[off] >> 4;
			if(fg == bar) fg = bar == COLOR_BLACK ? COLOR_WHITE : COLOR_BLACK;
			MATRIX[off] = (fg << 4) | bar;
		}
}

void plat_ShowMessage(char *str, char color)
{
	unsigned char fg;
	unsigned char bg;

	fg = COL_YELLOW;
	bg = COL_FIELD;
	if(color == HCOLOR_INVALID)
	{
		fg = COL_WHITE;
		bg = COL_RED;
	}
	put_span(LOG_X, SCREEN_HEIGHT - 1, str, LOG_W, fg, bg);
}

void plat_ClearMessage(void)
{
	put_span(LOG_X, SCREEN_HEIGHT - 1, (char *)"", LOG_W, COL_WHITE, COL_FIELD);
}

void plat_AddToLogWin(void)
{
	char i;
	char y;
	unsigned char fg;

	for(i = 0; i < (char)((8 * BOARD_PIECE_HEIGHT) - 2); ++i)
	{
		y = (char)(1 + i);
		if(undo_FindUndoLine((char)((8 * BOARD_PIECE_HEIGHT) - 3 - i)))
		{
			frontend_FormatLogString();
			fg = gColor[0] ? COL_WHITE : COL_BLACK;
			put_span(LOG_X, y, gLogStrBuffer, LOG_W, fg, COL_FIELD);
		}
		else
			put_span(LOG_X, y, (char *)"", LOG_W, COL_WHITE, COL_FIELD);
	}
}

void plat_AddToLogWinTop(void)
{
	plat_AddToLogWin();
}

int plat_ReadKeys(char blocking)
{
	int keyMask;

	for(;;)
	{
		keyMask = scan_keys();
		if(keyMask)
			return keyMask;
		if(!blocking)
			return 0;
		waitvblank();
	}
}

char plat_GetSeed(void)
{
	return VICIV.rasterline;
}

void plat_Shutdown(void)
{
	char i, channel;
	volatile char *palette;
	VICIV.palsel = 0;
	for(channel = 0; channel < 3; ++channel)
	{
		palette = (volatile char *)(0xd100 + (unsigned int)channel * 256);
		for(i = 0; i < 16; ++i)
			palette[i] = savedPalette[channel][i];
	}
	VICIV.palsel = saved[0x70];
	/* restore only writable display configuration, never IRQ status or
	 * collision registers.  the SDK restores BASIC mapping on return. */
	VICIV.sdbdrwd_msb |= VIC4_HOTREG_MASK;
	VICIV.ctrlb = saved[0x31];
	VICIV.ctrl2 = saved[0x16];
	VICIV.ctrl1 = saved[0x11];
	VICIV.sdbdrwd_msb = saved[0x5d] & ~VIC4_HOTREG_MASK;
	memcpy((void *)0xd048, saved + 0x48, 8);
	memcpy((void *)0xd058, saved + 0x58, 14);
	memcpy((void *)0xd068, saved + 0x68, 3);
	VICIV.disp_rows = saved[0x7b];
	VICIV.ctrlc = saved[0x54];
	VICIV.bordercol = saved[0x20];
	VICIV.spr_ena = saved[0x15];
	VICIV.sdbdrwd_msb = saved[0x5d];
}
