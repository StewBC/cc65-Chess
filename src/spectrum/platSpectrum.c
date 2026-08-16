/*
 *	platSpectrum.c
 *	cc65 Chess
 *
 *	ZX Spectrum.  32 columns, so the squares are 3x2 characters (24x16
 *	pixels) rather than the C64's 4x3.  files along the top, ranks on the
 *	left, log and "Think" in the seven columns that remain.
 *
 *	  0:  A  B  C  D  E  F  G  H  White
 *	  1: 8 [squares 3 wide]       e2-e4
 *	     ...                      ...
 *	 16: 1                        (newest)
 *	 22:                          Think
 *
 *	pieces are 24x16 1-bit silhouettes.  square colour is paper, side is
 *	ink (black / white) on cyan or green so both sides stay visible.
 *	the B display puts the same four numbers the C64 does, crammed into
 *	the corners.  you can redraw the bitmaps later.
 */

#include <string.h>
#include <stdlib.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"

/*-----------------------------------------------------------------------*/
#define SCREEN_WIDTH			32
#define SCREEN_HEIGHT			24
#define BOARD_X					1
#define BOARD_Y					1
#define BOARD_PIECE_WIDTH		3
#define BOARD_PIECE_HEIGHT		2
#define LOG_X					25
#define LOG_W					7
#define LOG_TOP					1
#define LOG_BOT					16
#define MSG_Y					22
#define SCROLL_FRAMES			4

#define INK_BLACK				0
#define INK_BLUE				1
#define INK_RED					2
#define INK_MAGENTA				3
#define INK_GREEN				4
#define INK_CYAN				5
#define INK_YELLOW				6
#define INK_WHITE				7
#define PAPER_BLACK				0
#define PAPER_BLUE				1
#define PAPER_RED				2
#define PAPER_MAGENTA			3
#define PAPER_GREEN				4
#define PAPER_CYAN				5
#define PAPER_YELLOW			6
#define PAPER_WHITE				7
#define ATTR_BRIGHT				0x40
#define ATTR_FLASH				0x80
#define MKATTR(i,p)				((char)(((p) << 3) | (i)))

#define DFILE					((char *)0x4000)
#define AFILE					((char *)0x5800)
#define ROM_FONT				((char *)0x3D00)
#define FRAMES					((unsigned int *)23672)

/*-----------------------------------------------------------------------*/
static unsigned char sc_prev[8];
static unsigned int su_lastFrame;

/* last lines we successfully read from the undo ring.  sccz80's search
 * stack can walk onto the ring; the next redraw would then print nothing.
 * keep what we already showed so a smash does not blank the column */
#define LOG_LINES				(LOG_BOT - LOG_TOP + 1)
static char sc_logText[LOG_LINES][7];
static char sc_logCol[LOG_LINES];
static char sc_logN;

static void plat_paintLog(void);

static const unsigned int sc_kbPort[8] =
{
	0xFEFE, 0xFDFE, 0xFBFE, 0xF7FE,
	0xEFFE, 0xDFFE, 0xBFFE, 0x7FFE
};

/* HCOLOR_* from types.h, used as a paper colour the way the C64 uses them
 * as a VIC colour.  1 white, 2 red, 3 magenta, 4 green, 5 cyan, 6 yellow */
static const char sc_hpaper[] =
{
	PAPER_BLACK,
	PAPER_WHITE,
	PAPER_RED,
	PAPER_MAGENTA,
	PAPER_GREEN,
	PAPER_CYAN,
	PAPER_YELLOW,
	PAPER_BLUE
};

/* 24x16, 3 bytes a row.  ROOK .. PAWN, matching piece-1 */
static const char gfxTiles[PAWN][48] =
{
	/* ROOK */
	{
		0x00, 0x00, 0x00,
		0x33, 0x33, 0x00,
		0x7f, 0xff, 0x80,
		0x3f, 0xff, 0x00,
		0x1f, 0xfe, 0x00,
		0x1f, 0xfe, 0x00,
		0x1f, 0xfe, 0x00,
		0x1f, 0xfe, 0x00,
		0x1f, 0xfe, 0x00,
		0x1f, 0xfe, 0x00,
		0x1f, 0xfe, 0x00,
		0x3f, 0xff, 0x00,
		0x7f, 0xff, 0x80,
		0x7f, 0xff, 0x80,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	},
	/* KNIGHT */
	{
		0x00, 0x00, 0x00,
		0x00, 0xfc, 0x00,
		0x07, 0xff, 0x80,
		0x0f, 0xcf, 0xc0,
		0x0f, 0xff, 0xc0,
		0x0f, 0xff, 0xc0,
		0x07, 0xff, 0x80,
		0x03, 0xff, 0x00,
		0x03, 0xff, 0x00,
		0x03, 0xff, 0x00,
		0x07, 0xff, 0x80,
		0x0f, 0xff, 0xc0,
		0x1f, 0xff, 0xe0,
		0x1f, 0xff, 0xe0,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	},
	/* BISHOP */
	{
		0x00, 0x00, 0x00,
		0x00, 0x18, 0x00,
		0x00, 0x3c, 0x00,
		0x00, 0x66, 0x00,
		0x00, 0x7e, 0x00,
		0x00, 0x3c, 0x00,
		0x00, 0x3c, 0x00,
		0x00, 0x3c, 0x00,
		0x00, 0x7e, 0x00,
		0x00, 0xff, 0x00,
		0x01, 0xff, 0x80,
		0x03, 0xff, 0xc0,
		0x07, 0xff, 0xe0,
		0x0f, 0xff, 0xf0,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	},
	/* QUEEN */
	{
		0x00, 0x00, 0x00,
		0x08, 0x92, 0x20,
		0x19, 0x9b, 0x30,
		0x0f, 0xff, 0xc0,
		0x07, 0xff, 0x80,
		0x03, 0xff, 0x00,
		0x03, 0xff, 0x00,
		0x03, 0xff, 0x00,
		0x07, 0xff, 0x80,
		0x0f, 0xff, 0xc0,
		0x1f, 0xff, 0xe0,
		0x1f, 0xff, 0xe0,
		0x0f, 0xff, 0xc0,
		0x1f, 0xff, 0xe0,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	},
	/* KING */
	{
		0x00, 0x00, 0x00,
		0x00, 0x18, 0x00,
		0x00, 0x7e, 0x00,
		0x00, 0x18, 0x00,
		0x03, 0xff, 0xc0,
		0x06, 0x31, 0x80,
		0x07, 0xff, 0x80,
		0x03, 0xff, 0x00,
		0x03, 0xff, 0x00,
		0x03, 0xff, 0x00,
		0x07, 0xff, 0x80,
		0x0f, 0xff, 0xc0,
		0x1f, 0xff, 0xe0,
		0x1f, 0xff, 0xe0,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	},
	/* PAWN */
	{
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x1e, 0x00,
		0x00, 0x3f, 0x00,
		0x00, 0x3f, 0x00,
		0x00, 0x1e, 0x00,
		0x00, 0x1e, 0x00,
		0x00, 0x3f, 0x00,
		0x00, 0x7f, 0x80,
		0x00, 0xff, 0xc0,
		0x01, 0xff, 0xe0,
		0x03, 0xff, 0xf0,
		0x07, 0xff, 0xf8,
		0x07, 0xff, 0xf8,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	}
};

/*-----------------------------------------------------------------------*/
static char *zx_pix(char col, char y)
{
	unsigned int a;
	unsigned int y16;

	y16 = y;
	a = 0x4000;
	a += (y16 & 0x00C0) << 5;
	a += (y16 & 0x0007) << 8;
	a += (y16 & 0x0038) << 2;
	a += col;
	return (char *)a;
}

/*-----------------------------------------------------------------------*/
static void zx_cls(char attr)
{
	memset(DFILE, 0, 6144);
	memset(AFILE, attr, 768);
}

/*-----------------------------------------------------------------------*/
static void putch(char x, char y, char c, char attr)
{
	char *src;
	char *dst;
	char i;

	if(x > 31 || y > 23)
		return;
	if(c < 32)
		c = 32;
	src = ROM_FONT + ((unsigned int)(c - 32) << 3);
	dst = zx_pix(x, y << 3);
	for(i = 0; i < 8; ++i)
	{
		*dst = src[i];
		dst += 256;
	}
	AFILE[(unsigned int)y * SCREEN_WIDTH + x] = attr;
}

/*-----------------------------------------------------------------------*/
static void puts_xy(char x, char y, char *s, char attr)
{
	while(*s)
		putch(x++, y, *s++, attr);
}

/*-----------------------------------------------------------------------*/
static void put_span(char x, char y, char *s, char n, char attr)
{
	char i;
	char c;

	for(i = 0; i < n; ++i)
	{
		c = s[i];
		if(!c)
			break;
		putch(x + i, y, c, attr);
	}
	for(; i < n; ++i)
		putch(x + i, y, ' ', attr);
}

/*-----------------------------------------------------------------------*/
static void fill_attr(char x, char y, char w, char h, char attr)
{
	char r;
	char c;
	char *p;

	for(r = 0; r < h; ++r)
	{
		p = AFILE + (unsigned int)(y + r) * SCREEN_WIDTH + x;
		for(c = 0; c < w; ++c)
			*p++ = attr;
	}
}

/*-----------------------------------------------------------------------*/
static void fill_pix(char x, char y, char w, char h, char val)
{
	char r;
	char c;
	char s;
	char *dst;

	for(r = 0; r < h; ++r)
	{
		dst = zx_pix(x, (y + r) << 3);
		for(s = 0; s < 8; ++s)
		{
			for(c = 0; c < w; ++c)
				dst[c] = val;
			dst += 256;
		}
	}
}

/*-----------------------------------------------------------------------*/
/* 3x2 cell: 16 rows of 3 bytes.  src NULL clears.  two zx_pix calls,
 * not one per byte - the C64 can be slow; this does not have to match it */
static void blit_square(char x, char y, const char *src)
{
	char r;
	char s;
	char *p;

	for(r = 0; r < 2; ++r)
	{
		p = zx_pix(x, (char)((y + r) << 3));
		for(s = 0; s < 8; ++s)
		{
			if(src)
			{
				p[0] = *src++;
				p[1] = *src++;
				p[2] = *src++;
			}
			else
			{
				p[0] = 0;
				p[1] = 0;
				p[2] = 0;
			}
			p += 256;
		}
	}
}

/*-----------------------------------------------------------------------*/
static void draw_piece(char x, char y, const char *src)
{
	blit_square(x, y, src);
}

/*-----------------------------------------------------------------------*/
static char plat_TimeExpired(unsigned int ticks)
{
	unsigned int now;

	now = *FRAMES;
	if((unsigned int)(now - su_lastFrame) >= ticks)
	{
		su_lastFrame = now;
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
static char digit_or_hex(char n)
{
	if(n < 10)
		return (char)('0' + n);
	return (char)('A' + (n - 10));
}

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
	char attr;

	/* Setting this to 0 will not show the "Quit" option in the main menu */
	gReturnToOS = 1;

	memset(sc_prev, 0xFF, 8);
	outp(0xFE, INK_BLACK);
	attr = MKATTR(INK_WHITE, PAPER_BLACK);
	zx_cls(attr);

	/* 32 columns, and gszAbout is 36 visible characters - one row cannot
	 * hold it.  putch clips at x>31 rather than wrapping, so drawn from
	 * x=1 the year simply fell off the right edge and the splash read
	 * "...S. Wessels,".  Split at the space after the version, both
	 * halves centred */
	attr = MKATTR(INK_YELLOW, PAPER_BLACK) | ATTR_BRIGHT;
	put_span(8, 9, gszAbout, 15, attr);
	puts_xy(6, 10, gszAbout + 16, attr);
	puts_xy(3, 12, "ZX Spectrum version, 2026.", MKATTR(INK_CYAN, PAPER_BLACK));

	/* two kings, black above the text and white below - the same welcome
	 * every other port draws.  the text is rows 9..12, so 5-6 and 15-16
	 * sit five rows either side of its centre.  attributes are the
	 * board's: a white piece is bright white ink, a black one is not */
	fill_attr(14, 5, 3, 2, MKATTR(INK_BLACK, PAPER_GREEN));
	draw_piece(14, 5, gfxTiles[KING - 1]);
	fill_attr(14, 15, 3, 2, MKATTR(INK_WHITE, PAPER_GREEN) | ATTR_BRIGHT);
	draw_piece(14, 15, gfxTiles[KING - 1]);
	puts_xy(10, 20, "Press ENTER", MKATTR(INK_WHITE, PAPER_BLACK) | ATTR_FLASH);

	plat_ReadKeys(1);
}

/*-----------------------------------------------------------------------*/
void plat_UpdateScreen(void)
{
}

/*-----------------------------------------------------------------------*/
char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller;
	int keyMask;
	char i, sx, sy, numMenuItems, maxLen;
	char attr, hattr, tattr, sattr;
	char len;

	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
	}

	maxLen = 0;
	for(numMenuItems = 0; menuItems[numMenuItems]; ++numMenuItems)
	{
		len = (char)strlen(menuItems[numMenuItems]);
		if(len > maxLen)
			maxLen = len;
	}

	if(maxLen > SCREEN_WIDTH - 4)
		maxLen = SCREEN_WIDTH - 4;

	sy = (char)((SCREEN_HEIGHT / 2) - (height / 2) - 1);
	sx = (char)((SCREEN_WIDTH / 2) - (maxLen / 2) - 1);

	attr = MKATTR(INK_WHITE, PAPER_BLUE);
	hattr = MKATTR(INK_YELLOW, PAPER_BLUE) | ATTR_BRIGHT;
	tattr = MKATTR(INK_BLUE, PAPER_YELLOW) | ATTR_BRIGHT;
	sattr = MKATTR(INK_CYAN, PAPER_BLUE);

	fill_pix(sx - 1, sy - 1, maxLen + 4, height + 4, 0);
	fill_attr(sx - 1, sy - 1, maxLen + 4, height + 4,
		MKATTR(INK_YELLOW, PAPER_BLUE) | ATTR_BRIGHT);

	put_span(sx, sy, menuItems[0], maxLen + 2, hattr);
	put_span(sx, sy + 1, "", maxLen + 2, attr);

	for(i = 1; i < numMenuItems; ++i)
		put_span(sx, sy + 1 + i, menuItems[i], maxLen + 2, attr);
	for(; i < height; ++i)
		put_span(sx, sy + 1 + i, "", maxLen + 2, attr);

	i = 1;
	do
	{
		putch(sx, sy + 1 + i, '>', tattr);
		put_span(sx + 1, sy + 1 + i, menuItems[i], maxLen, tattr);
		putch(sx + 1 + maxLen, sy + 1 + i, '<', tattr);

		keyMask = plat_ReadKeys(0);
		if(keyMask & INPUT_MOTION)
		{
			putch(sx, sy + 1 + i, ' ', attr);
			put_span(sx + 1, sy + 1 + i, menuItems[i], maxLen, attr);
			putch(sx + 1 + maxLen, sy + 1 + i, ' ', attr);
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

		{
			char si;
			char *sp;

			sp = pScroller;
			for(si = 0; si < maxLen + 2; ++si)
			{
				if(!*sp)
					sp = scroller;
				putch((char)(sx + si), (char)(sy + 1 + height),
					*sp ? *sp : ' ', sattr);
				if(*sp)
					++sp;
			}
		}

		if(plat_TimeExpired(SCROLL_FRAMES))
		{
			++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
	} while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);

	if(keyMask & INPUT_BACKUP)
		return 0;
	return i;
}

/*-----------------------------------------------------------------------*/
void plat_DrawBoard(char clearLog)
{
	char i;
	char attr;

	/* always wipe.  the menu sits on the bitmap and a partial redraw
	 * would leave its box behind */
	attr = MKATTR(INK_WHITE, PAPER_BLACK);
	zx_cls(attr);
	if(clearLog)
		sc_logN = 0;
	else
		plat_paintLog();

	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);

	for(i = 0; i < 8; ++i)
	{
		putch((char)(BOARD_X + i * BOARD_PIECE_WIDTH + 1), 0,
			(char)('A' + i), MKATTR(INK_YELLOW, PAPER_BLACK));
		putch(0, (char)(BOARD_Y + i * BOARD_PIECE_HEIGHT),
			(char)('8' - i), MKATTR(INK_YELLOW, PAPER_BLACK));
	}
}

/*-----------------------------------------------------------------------*/
void plat_DrawSquare(char position)
{
	char y;
	char x;
	char light;
	char sx;
	char sy;
	char piece;
	char colour;
	char paper;
	char ink;
	char attr;

	y = position / 8;
	x = position & 7;
	light = !((x & 1) ^ (y & 1));
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);

	/* blue / green: white ink is visible on both.  cyan ate the white pieces */
	paper = light ? PAPER_BLUE : PAPER_GREEN;
	piece = gChessBoard[y][x];
	colour = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	ink = piece ? (colour ? INK_WHITE : INK_BLACK) : paper;
	attr = MKATTR(ink, paper);
	if(colour)
		attr |= ATTR_BRIGHT;

	blit_square(sx, sy, piece ? gfxTiles[piece - 1] : 0);
	fill_attr(sx, sy, BOARD_PIECE_WIDTH, BOARD_PIECE_HEIGHT, attr);

	if(gShowAttackBoard)
	{
		/* attackers bottom left, defenders bottom right, value top left,
		 * colour top right.  same four numbers as the C64, one glyph each */
		putch(sx, (char)(sy + 1),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][0]]),
			MKATTR(INK_RED, paper));
		putch((char)(sx + 2), (char)(sy + 1),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][1]]),
			MKATTR(INK_BLUE, paper));
		putch(sx, sy,
			digit_or_hex((char)(gChessBoard[y][x] & 0x0F)),
			MKATTR(INK_MAGENTA, paper));
		putch((char)(sx + 2), sy,
			digit_or_hex((char)(colour >> 7)),
			MKATTR(INK_YELLOW, paper));
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	char attr;

	attr = side
		? (MKATTR(INK_BLACK, PAPER_WHITE) | ATTR_BRIGHT)
		: MKATTR(INK_YELLOW, PAPER_BLACK);
	put_span(LOG_X, 0, gszSideLabel[side], LOG_W, attr);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char cursor)
{
	char y;
	char x;
	char sx;
	char sy;
	char paper;
	char ink;
	char attr;
	char r;

	y = position / 8;
	x = position & 7;
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);
	paper = sc_hpaper[color & 7];
	ink = (paper == PAPER_BLACK || paper == PAPER_BLUE) ? INK_WHITE : INK_BLACK;
	attr = MKATTR(ink, paper) | ATTR_BRIGHT;
	if(cursor)
		attr |= ATTR_FLASH;

	for(r = 0; r < BOARD_PIECE_HEIGHT; ++r)
	{
		AFILE[(unsigned int)(sy + r) * SCREEN_WIDTH + sx] = attr;
		AFILE[(unsigned int)(sy + r) * SCREEN_WIDTH + sx + 2] = attr;
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char color)
{
	char attr;

	attr = MKATTR(INK_YELLOW, PAPER_BLACK) | ATTR_BRIGHT;
	if(color == HCOLOR_VALID)
		attr |= ATTR_FLASH;
	if(color == HCOLOR_INVALID)
		attr = MKATTR(INK_WHITE, PAPER_RED) | ATTR_BRIGHT;
	put_span(LOG_X, MSG_Y, str, LOG_W, attr);
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage(void)
{
	put_span(LOG_X, MSG_Y, "", LOG_W, MKATTR(INK_WHITE, PAPER_BLACK));
}

/*-----------------------------------------------------------------------*/
static void plat_paintLog(void)
{
	char i;
	char y;
	char attr;

	for(i = 0; i < LOG_LINES; ++i)
	{
		y = (char)(LOG_TOP + i);
		if(i < sc_logN)
		{
			attr = sc_logCol[i]
				? (MKATTR(INK_WHITE, PAPER_BLACK) | ATTR_BRIGHT)
				: (MKATTR(INK_YELLOW, PAPER_BLACK) | ATTR_BRIGHT);
			put_span(LOG_X, y, sc_logText[i], LOG_W, attr);
		}
		else
			put_span(LOG_X, y, "", LOG_W, MKATTR(INK_WHITE, PAPER_BLACK));
	}
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWin(void)
{
	char i;

	/* frontend_LogMove is the only caller.  the other ports repaint the
	 * whole column out of the undo ring and fill gTile as a side effect;
	 * this one keeps its own scrollback, so it asks for the top entry
	 * itself rather than making board_ApplyMove fill gTile for everybody.
	 * (0) is the move just pushed - after a redo too, which ApplyMove
	 * never sees.  Do not invent a line from leftover gTile: DrawBoard
	 * after the menu used to call this and that is the AH-AH at the
	 * bottom of the log - no move had been played.
	 *
	 * there is deliberately no "same as the line above" check.  one was
	 * here to swallow the duplicate a redo used to produce off stale
	 * globals; asking the ring makes each call a real move, and two
	 * consecutive plies are by opposite sides so they cannot share a
	 * from-to pair anyway */
	if(!undo_FindUndoLine(0))
		return;
	frontend_FormatLogString();

	if(sc_logN < LOG_LINES)
		++sc_logN;
	for(i = (char)(sc_logN - 1); i > 0; --i)
	{
		memcpy(sc_logText[i], sc_logText[i - 1], 7);
		sc_logCol[i] = sc_logCol[i - 1];
	}
	memcpy(sc_logText[0], gLogStrBuffer, 7);
	sc_logCol[0] = gColor[0];
	plat_paintLog();
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWinTop(void)
{
	if(sc_logN)
		--sc_logN;
	plat_paintLog();
}

/*-----------------------------------------------------------------------*/
int plat_ReadKeys(char blocking)
{
	unsigned char now[8];
	int keyMask;
	char i;
	char caps;

	for(;;)
	{
		for(i = 0; i < 8; ++i)
			now[i] = (unsigned char)inp(sc_kbPort[i]);

		keyMask = 0;
		caps = !(now[0] & 0x01);

		/* caps+5/6/7/8 are the 48K cursor keys, and the 128K cursors
		 * close the same matrix lines */
		if(caps && !(now[4] & 0x08) && (sc_prev[4] & 0x08))
			keyMask |= INPUT_UP;
		if(caps && !(now[4] & 0x10) && (sc_prev[4] & 0x10))
			keyMask |= INPUT_DOWN;
		if(caps && !(now[3] & 0x10) && (sc_prev[3] & 0x10))
			keyMask |= INPUT_LEFT;
		if(caps && !(now[4] & 0x04) && (sc_prev[4] & 0x04))
			keyMask |= INPUT_RIGHT;

		if(!(now[6] & 0x01) && (sc_prev[6] & 0x01))
			keyMask |= INPUT_SELECT;
		if(!(now[7] & 0x01) && (sc_prev[7] & 0x01))
			keyMask |= INPUT_BACKUP;

		if(!(now[1] & 0x01) && (sc_prev[1] & 0x01))
			keyMask |= INPUT_TOGGLE_A;
		if(!(now[7] & 0x10) && (sc_prev[7] & 0x10))
			keyMask |= INPUT_TOGGLE_B;
		if(!(now[1] & 0x04) && (sc_prev[1] & 0x04))
			keyMask |= INPUT_TOGGLE_D;
		if(!(now[7] & 0x04) && (sc_prev[7] & 0x04))
			keyMask |= INPUT_MENU;
		if(!(now[5] & 0x08) && (sc_prev[5] & 0x08))
			keyMask |= INPUT_UNDO;
		if(!(now[2] & 0x08) && (sc_prev[2] & 0x08))
			keyMask |= INPUT_REDO;

		memcpy(sc_prev, now, 8);
		if(keyMask || !blocking)
			return keyMask;
	}
}

/*-----------------------------------------------------------------------*/
void plat_Shutdown(void)
{
	outp(0xFE, INK_WHITE);
}

/*-----------------------------------------------------------------------*/
char plat_GetSeed(void)
{
	/* FRAMES at 23672, 50 Hz.  A wrong address repeats openings. */
	return *(char *)23672;
}
