/*
 *	platTi99.c
 *	cc65 Chess
 *
 *	TI-99/4A, TMS9918 Graphics II.  32 columns, so the squares are 3x2
 *	characters like the Spectrum.  C64 colours: green field, white/black
 *	pieces on light/dark green, yellow frame, yellow-on-blue menus.
 */

#include "string.h"
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"

extern const char sc_font[96][8];

void vdp_wa(unsigned int addr, unsigned int write);
void vdp_reg(unsigned int reg, unsigned char val);
void vdp_put(unsigned char b);
unsigned char vdp_status(void);
unsigned int kscan_col(unsigned int col);
void kscan_init(void);
void ti_reboot(void);

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
#define SCROLL_FRAMES			2
#define LOG_LINES				(LOG_BOT - LOG_TOP + 1)

#define TI_TRANS				0
#define TI_BLACK				1
#define TI_MEDGREEN				2
#define TI_LTGREEN				3
#define TI_DKBLUE				4
#define TI_LTBLUE				5
#define TI_DKRED				6
#define TI_CYAN					7
#define TI_MEDRED				8
#define TI_LTRED				9
#define TI_DKYELLOW				10
#define TI_LTYELLOW				11
#define TI_DKGREEN				12
#define TI_MAGENTA				13
#define TI_GRAY					14
#define TI_WHITE				15

#define COL_BG					TI_DKGREEN
#define COL_LIGHT				TI_LTGREEN
#define COL_DARK				TI_DKGREEN
#define COL_FRAME				TI_LTYELLOW
#define COL_MENU_BG				TI_DKBLUE
#define COL_MENU_TITLE			TI_LTYELLOW
#define COL_MENU_ITEM			TI_GRAY
#define COL_MENU_SEL			TI_WHITE
#define COL_SCROLL				TI_CYAN

#define MKCOL(fg, bg)			((unsigned char)(((fg) << 4) | (bg)))

/*-----------------------------------------------------------------------*/
static unsigned char sc_prev[6];
static unsigned int su_frames;

/* HCOLOR_* as a square colour, the way Spectrum remaps them onto paper */
static const char sc_hcol[] =
{
	TI_BLACK,
	TI_WHITE,
	TI_MEDRED,
	TI_MAGENTA,
	TI_MEDGREEN,
	TI_CYAN,
	TI_LTYELLOW,
	TI_DKBLUE
};

/* 24x16 silhouettes, Spectrum's set */
static const char gfxTiles[PAWN][48] =
{
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
	{
		/* king.  the spectrum set had the body one pixel left of
		 * the cross, so the bar under the cross overhung only on
		 * the right.  every row is centred on pixel 11.5. */
		0x00, 0x00, 0x00,
		0x00, 0x18, 0x00,
		0x00, 0x7e, 0x00,
		0x00, 0x18, 0x00,
		0x03, 0xff, 0xc0,
		0x03, 0x18, 0xc0,
		0x03, 0xff, 0xc0,
		0x01, 0xff, 0x80,
		0x01, 0xff, 0x80,
		0x01, 0xff, 0x80,
		0x03, 0xff, 0xc0,
		0x07, 0xff, 0xe0,
		0x0f, 0xff, 0xf0,
		0x0f, 0xff, 0xf0,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	},
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
static unsigned int pat_addr(char x, char y)
{
	unsigned int bank;
	unsigned int idx;

	bank = (unsigned int)(y >> 3);
	idx = ((unsigned int)(y & 7) << 5) + (unsigned int)x;
	return (bank << 11) + (idx << 3);
}

static void vdp_fill(unsigned int addr, unsigned char val, unsigned int n)
{
	vdp_wa(addr, 1);
	while(n)
	{
		vdp_put(val);
		--n;
	}
}

static void cell_pattern(char x, char y, const char *src, unsigned char col)
{
	unsigned int p;
	unsigned int c;
	char i;

	p = pat_addr(x, y);
	c = p + 0x2000;
	vdp_wa(p, 1);
	for(i = 0; i < 8; ++i)
		vdp_put(src ? src[i] : 0);
	vdp_wa(c, 1);
	for(i = 0; i < 8; ++i)
		vdp_put(col);
}

static void cell_color(char x, char y, unsigned char col)
{
	char i;

	vdp_wa(pat_addr(x, y) + 0x2000, 1);
	for(i = 0; i < 8; ++i)
		vdp_put(col);
}

static void putch(char x, char y, char ch, unsigned char col)
{
	const char *src;

	if(x > 31 || y > 23)
		return;
	if(ch < 32 || ch > 127)
		ch = 32;
	src = sc_font[ch - 32];
	cell_pattern(x, y, src, col);
}

static void puts_xy(char x, char y, char *s, unsigned char col)
{
	while(*s)
		putch(x++, y, *s++, col);
}

static void put_span(char x, char y, char *s, char n, unsigned char col)
{
	char i;
	char c;

	for(i = 0; i < n; ++i)
	{
		c = s[i];
		if(!c)
			break;
		putch(x + i, y, c, col);
	}
	for(; i < n; ++i)
		putch(x + i, y, ' ', col);
}

static void blit_square(char x, char y, const char *src, unsigned char col)
{
	char r;
	char s;
	char row[8];
	char i;

	for(r = 0; r < 2; ++r)
	{
		for(s = 0; s < 3; ++s)
		{
			if(src)
			{
				for(i = 0; i < 8; ++i)
					row[i] = src[i * 3 + s];
				cell_pattern((char)(x + s), (char)(y + r), row, col);
			}
			else
				cell_pattern((char)(x + s), (char)(y + r), 0, col);
		}
		if(src)
			src += 24;
	}
}

static void ti_cls(unsigned char col)
{
	unsigned int i;

	vdp_fill(0x0000, 0, 6144);
	vdp_fill(0x2000, col, 6144);
	vdp_wa(0x1800, 1);
	for(i = 0; i < 768; ++i)
		vdp_put((unsigned char)(i & 255));
}

static void g2_init(void)
{
	vdp_reg(1, 0x80);
	vdp_reg(0, 0x02);
	vdp_reg(2, 0x06);
	vdp_reg(3, 0xFF);
	vdp_reg(4, 0x03);
	vdp_reg(5, 0x36);
	vdp_reg(6, 0x07);
	vdp_reg(7, COL_BG);
	vdp_fill(0x1B00, 0xD0, 1);
	vdp_reg(1, 0xC0);
}

static char plat_TimeExpired(unsigned int ticks)
{
	/* vdp_status is a char: the 9918 puts it in the high byte of R1.
	 * testing 0x80 on an unsigned int never saw the frame flag, so the
	 * menu scroller sat still.  F is set at vblank even with IE off. */
	if(vdp_status() & 0x80)
		++su_frames;
	if(su_frames >= ticks)
	{
		su_frames = 0;
		return 1;
	}
	return 0;
}

static char digit_or_hex(char n)
{
	if(n < 10)
		return (char)('0' + n);
	return (char)('A' + (n - 10));
}

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
	unsigned char col;

	gReturnToOS = 1;
	kscan_init();
	memset(sc_prev, 0, 6);
	g2_init();
	col = MKCOL(TI_WHITE, COL_BG);
	ti_cls(col);

	col = MKCOL(TI_LTYELLOW, COL_BG);
	put_span(8, 9, gszAbout, 15, col);
	puts_xy(6, 10, gszAbout + 16, col);
	puts_xy(4, 12, "TI-99/4A version, 2026.", MKCOL(TI_CYAN, COL_BG));

	blit_square(14, 5, gfxTiles[KING - 1], MKCOL(TI_BLACK, COL_LIGHT));
	blit_square(14, 15, gfxTiles[KING - 1], MKCOL(TI_WHITE, COL_LIGHT));
	puts_xy(10, 20, "Press ENTER", MKCOL(TI_WHITE, COL_BG));

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
	unsigned char attr, hattr, tattr, sattr;
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

	attr = MKCOL(COL_MENU_ITEM, COL_MENU_BG);
	hattr = MKCOL(COL_MENU_TITLE, COL_MENU_BG);
	tattr = MKCOL(COL_MENU_SEL, COL_MENU_BG);
	sattr = MKCOL(COL_SCROLL, COL_MENU_BG);

	{
		char r, c, k;
		unsigned char frame;
		char solid[8];

		for(k = 0; k < 8; ++k)
			solid[k] = (char)0xFF;
		frame = MKCOL(COL_FRAME, COL_MENU_BG);
		for(r = 0; r < (char)(height + 4); ++r)
		{
			for(c = 0; c < (char)(maxLen + 4); ++c)
			{
				if(r == 0 || r == (char)(height + 3) ||
					c == 0 || c == (char)(maxLen + 3))
					cell_pattern((char)(sx - 1 + c), (char)(sy - 1 + r),
						solid, frame);
				else
					cell_pattern((char)(sx - 1 + c), (char)(sy - 1 + r), 0,
						MKCOL(COL_MENU_ITEM, COL_MENU_BG));
			}
		}
	}

	put_span(sx, sy, menuItems[0], (char)(maxLen + 2), hattr);
	put_span(sx, (char)(sy + 1), "", (char)(maxLen + 2), attr);
	/* inset like the C64's " %.*s " so the first paint matches a
	 * deselected line.  drawing at sx left the text under the '>'. */
	for(i = 1; i < numMenuItems; ++i)
	{
		putch(sx, (char)(sy + 1 + i), ' ', attr);
		put_span((char)(sx + 1), (char)(sy + 1 + i), menuItems[i], maxLen, attr);
		putch((char)(sx + 1 + maxLen), (char)(sy + 1 + i), ' ', attr);
	}
	for(; i < height; ++i)
		put_span(sx, (char)(sy + 1 + i), "", (char)(maxLen + 2), attr);

	i = 1;
	do
	{
		putch(sx, (char)(sy + 1 + i), '>', tattr);
		put_span((char)(sx + 1), (char)(sy + 1 + i), menuItems[i], maxLen, tattr);
		putch((char)(sx + 1 + maxLen), (char)(sy + 1 + i), '<', tattr);

		keyMask = plat_ReadKeys(0);
		if(keyMask & INPUT_MOTION)
		{
			putch(sx, (char)(sy + 1 + i), ' ', attr);
			put_span((char)(sx + 1), (char)(sy + 1 + i), menuItems[i], maxLen, attr);
			putch((char)(sx + 1 + maxLen), (char)(sy + 1 + i), ' ', attr);
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
			for(si = 0; si < (char)(maxLen + 2); ++si)
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

void plat_DrawBoard(char clearLog)
{
	char i;

	(void)clearLog;
	ti_cls(MKCOL(TI_WHITE, COL_BG));
	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);
	for(i = 0; i < 8; ++i)
	{
		putch((char)(BOARD_X + i * BOARD_PIECE_WIDTH + 1), 0,
			(char)('A' + i), MKCOL(COL_FRAME, COL_BG));
		putch(0, (char)(BOARD_Y + i * BOARD_PIECE_HEIGHT),
			(char)('8' - i), MKCOL(COL_FRAME, COL_BG));
	}
	if(!clearLog)
		plat_AddToLogWin();
}

void plat_DrawSquare(char position)
{
	char y;
	char x;
	char light;
	char sx;
	char sy;
	char piece;
	char colour;
	unsigned char paper;
	unsigned char ink;
	unsigned char col;

	y = position / 8;
	x = position & 7;
	light = !((x & 1) ^ (y & 1));
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);

	paper = light ? COL_LIGHT : COL_DARK;
	piece = gChessBoard[y][x];
	colour = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	ink = piece ? (colour ? TI_WHITE : TI_BLACK) : paper;
	col = MKCOL(ink, paper);

	blit_square(sx, sy, piece ? gfxTiles[piece - 1] : 0, col);

	if(gShowAttackBoard)
	{
		putch(sx, (char)(sy + 1),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][0]]),
			MKCOL(TI_MEDRED, paper));
		putch((char)(sx + 2), (char)(sy + 1),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][1]]),
			MKCOL(TI_DKBLUE, paper));
		putch(sx, sy,
			digit_or_hex((char)(gChessBoard[y][x] & 0x0F)),
			MKCOL(TI_MAGENTA, paper));
		putch((char)(sx + 2), sy,
			digit_or_hex((char)(colour >> 7)),
			MKCOL(TI_LTYELLOW, paper));
	}
}

void plat_ShowSideToGoLabel(char side)
{
	unsigned char col;

	col = side ? MKCOL(TI_BLACK, TI_WHITE) : MKCOL(TI_LTYELLOW, COL_BG);
	put_span(LOG_X, 0, gszSideLabel[side], LOG_W, col);
}

void plat_Highlight(char position, char color, char cursor)
{
	char y;
	char x;
	char sx;
	char sy;
	unsigned char paper;
	unsigned char ink;
	unsigned char col;
	char r;

	y = position / 8;
	x = position & 7;
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);
	paper = sc_hcol[color & 7];
	ink = (paper == TI_BLACK || paper == TI_DKBLUE) ? TI_WHITE : TI_BLACK;
	col = MKCOL(ink, paper);
	(void)cursor;

	for(r = 0; r < BOARD_PIECE_HEIGHT; ++r)
	{
		cell_color(sx, (char)(sy + r), col);
		cell_color((char)(sx + 2), (char)(sy + r), col);
	}
}

void plat_ShowMessage(char *str, char color)
{
	unsigned char col;

	col = MKCOL(TI_LTYELLOW, COL_BG);
	if(color == HCOLOR_INVALID)
		col = MKCOL(TI_WHITE, TI_MEDRED);
	put_span(LOG_X, MSG_Y, str, LOG_W, col);
}

void plat_ClearMessage(void)
{
	put_span(LOG_X, MSG_Y, "", LOG_W, MKCOL(TI_WHITE, COL_BG));
}

void plat_AddToLogWin(void)
{
	char i;
	char y;
	unsigned char col;

	for(i = 0; i < LOG_LINES; ++i)
	{
		y = (char)(LOG_TOP + i);
		if(undo_FindUndoLine((char)(LOG_LINES - 1 - i)))
		{
			frontend_FormatLogString();
			col = gColor[0]
				? MKCOL(TI_WHITE, COL_BG)
				: MKCOL(TI_LTYELLOW, COL_BG);
			put_span(LOG_X, y, gLogStrBuffer, LOG_W, col);
		}
		else
			put_span(LOG_X, y, "", LOG_W, MKCOL(TI_WHITE, COL_BG));
	}
}

void plat_AddToLogWinTop(void)
{
	plat_AddToLogWin();
}

int plat_ReadKeys(char blocking)
{
	unsigned char now[6];
	int keyMask;
	char i;
	char fctn;

	for(;;)
	{
		for(i = 0; i < 6; ++i)
			now[i] = (unsigned char)kscan_col((unsigned int)i);

		keyMask = 0;
		fctn = (char)(now[0] & 0x08);

		if(fctn && (now[2] & 0x02) && !(sc_prev[2] & 0x02))
			keyMask |= INPUT_UP;
		if(fctn && (now[1] & 0x01) && !(sc_prev[1] & 0x01))
			keyMask |= INPUT_DOWN;
		if(fctn && (now[1] & 0x04) && !(sc_prev[1] & 0x04))
			keyMask |= INPUT_LEFT;
		if(fctn && (now[2] & 0x04) && !(sc_prev[2] & 0x04))
			keyMask |= INPUT_RIGHT;

		if((now[0] & 0x20) && !(sc_prev[0] & 0x20))
			keyMask |= INPUT_SELECT;
		if(fctn && (now[1] & 0x10) && !(sc_prev[1] & 0x10))
			keyMask |= INPUT_BACKUP;

		if(!fctn && (now[5] & 0x04) && !(sc_prev[5] & 0x04))
			keyMask |= INPUT_TOGGLE_A;
		if(!fctn && (now[4] & 0x01) && !(sc_prev[4] & 0x01))
			keyMask |= INPUT_TOGGLE_B;
		if(!fctn && (now[2] & 0x04) && !(sc_prev[2] & 0x04))
			keyMask |= INPUT_TOGGLE_D;
		if(!fctn && (now[3] & 0x80) && !(sc_prev[3] & 0x80))
			keyMask |= INPUT_MENU;
		if(!fctn && (now[3] & 0x20) && !(sc_prev[3] & 0x20))
			keyMask |= INPUT_UNDO;
		if(!fctn && (now[3] & 0x02) && !(sc_prev[3] & 0x02))
			keyMask |= INPUT_REDO;

		memcpy(sc_prev, now, 6);
		if(keyMask || !blocking)
			return keyMask;
	}
}

void plat_Shutdown(void)
{
	ti_reboot();
}

char plat_GetSeed(void)
{
	return vdp_status();
}
