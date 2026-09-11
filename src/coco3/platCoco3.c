/*
 *	platCoco3.c
 *	cc65 Chess
 *
 *	Color Computer 3, GIME 320x200x16.  C64 layout: 40 columns, 4x3
 *	squares, 32x24 pieces, green field, yellow-on-blue menus.  RGB
 *	palette by default.  hold C on the splash for composite (not advertised).
 */

#include <cmoc.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"

extern const char sc_font[96][8];
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
#define BPL						160
#define FB_WIN					0x8000
#define FB_BLOCK				0x30
#define SCROLL_FRAMES			4

#define COL_BG					0
#define COL_BLACK				1
#define COL_WHITE				2
#define COL_YELLOW				3
#define COL_BLUE				4
#define COL_CYAN				5
#define COL_RED					6
#define COL_MAGENTA				7
#define COL_GRAY				8
#define COL_DGRAY				9
#define COL_LGRAY				10
#define COL_DGREEN				11

/* HCOLOR_* as a bar colour, the way the C64 uses VIC colours */
static const char sc_hcol[] =
{
	COL_BLACK,
	COL_WHITE,
	COL_RED,
	COL_MAGENTA,
	COL_DGREEN,
	COL_CYAN,
	COL_YELLOW,
	COL_BLUE
};

/* GIME 6-bit is RGBRGB (high then low), not RRGGBB.  0x12 is BASIC's green. */
static const unsigned char pal_rgb[16] =
{
	0x12, 0x00, 0x3F, 0x36,
	0x09, 0x1B, 0x24, 0x2D,
	0x38, 0x07, 0x12, 0x10,
	0x24, 0x0A, 0x25, 0x1C
};

static const unsigned char pal_cmp[16] =
{
	0x12, 0x00, 0x3F, 0x36,
	0x08, 0x1B, 0x24, 0x26,
	0x38, 0x07, 0x12, 0x10,
	0x20, 0x0A, 0x25, 0x1C
};

static unsigned char sc_bank;
static unsigned char sc_rgb;
static unsigned char sc_prev[8];
static unsigned char su_frames;
static char textStr[41];
static char subMenu;

/*-----------------------------------------------------------------------*/
static void map_fb(unsigned char bank)
{
	if(bank != sc_bank)
	{
		*(unsigned char *)0xFFA4 = (unsigned char)(FB_BLOCK + bank);
		sc_bank = bank;
	}
}

static unsigned char *fb_at(unsigned int off)
{
	map_fb((unsigned char)(off >> 13));
	return (unsigned char *)(FB_WIN + (off & 0x1FFF));
}

static void fb_fill(unsigned int off, unsigned int n, unsigned char byte)
{
	unsigned int chunk;
	unsigned char *p;

	while(n)
	{
		chunk = 8192 - (off & 0x1FFF);
		if(chunk > n)
			chunk = n;
		p = fb_at(off);
		memset(p, byte, chunk);
		off += chunk;
		n -= chunk;
	}
}

static void fill_rect(unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned char color)
{
	unsigned int row;
	unsigned char byte;

	byte = (unsigned char)((color << 4) | (color & 0x0F));
	for(row = 0; row < h; ++row)
		fb_fill((y + row) * BPL + (x >> 1), w >> 1, byte);
}

static void apply_palette(unsigned char rgb)
{
	const unsigned char *p;
	unsigned char i;

	p = rgb ? pal_rgb : pal_cmp;
	for(i = 0; i < 16; ++i)
		*(unsigned char *)(0xFFB0 + i) = p[i];
}

static void gime_init(void)
{
	unsigned char i;

	asm { orcc #$50 }
	*(unsigned char *)0xFF90 = 0x4C;
	*(unsigned char *)0xFFD9 = 0;

	/* PIA0: rows on A (input), columns on B (output).  BASIC left
	 * this set up; say so after we take the machine. */
	*(unsigned char *)0xFF01 = 0;
	*(unsigned char *)0xFF00 = 0;
	*(unsigned char *)0xFF01 = 0x04;
	*(unsigned char *)0xFF03 = 0;
	*(unsigned char *)0xFF02 = 0xFF;
	*(unsigned char *)0xFF03 = 0x34;
	sc_bank = 255;
	map_fb(0);

	*(unsigned char *)0xFF98 = 0x80;
	*(unsigned char *)0xFF99 = 0x3E;
	*(unsigned char *)0xFF9A = 0x12;
	*(unsigned char *)0xFF9C = 0;
	*(unsigned char *)0xFF9D = 0xC0;
	*(unsigned char *)0xFF9E = 0;
	*(unsigned char *)0xFF9F = 0;

	apply_palette(sc_rgb);
	for(i = 0; i < 4; ++i)
	{
		map_fb(i);
		memset((unsigned char *)FB_WIN, (unsigned char)((COL_BG << 4) | COL_BG), 8192);
	}
}

static void putch(char x, char y, char ch, unsigned char fg, unsigned char bg)
{
	const char *src;
	unsigned int off;
	unsigned char row;
	unsigned char k;
	unsigned char b;
	unsigned char hi;
	unsigned char lo;

	if((unsigned char)x >= SCREEN_WIDTH || (unsigned char)y >= SCREEN_HEIGHT)
		return;
	if(ch < 32 || ch > 127)
		ch = 32;
	src = sc_font[ch - 32];
	off = ((unsigned int)y << 3) * BPL + ((unsigned int)x << 2);
	for(row = 0; row < 8; ++row)
	{
		b = src[row];
		for(k = 0; k < 4; ++k)
		{
			hi = (unsigned char)((b & 0x80) ? fg : bg);
			lo = (unsigned char)((b & 0x40) ? fg : bg);
			*fb_at(off + k) = (unsigned char)((hi << 4) | lo);
			b <<= 2;
		}
		off += BPL;
	}
}

static void put_span(char x, char y, char *s, char n, unsigned char fg, unsigned char bg)
{
	char i;
	char c;

	for(i = 0; i < n; ++i)
	{
		c = s[i];
		if(!c)
			break;
		putch((char)(x + i), y, c, fg, bg);
	}
	for(; i < n; ++i)
		putch((char)(x + i), y, ' ', fg, bg);
}

static void blit_piece(char sx, char sy, const char *src, unsigned char ink, unsigned char paper)
{
	char cr;
	char cell;
	char row;
	char k;
	unsigned char b;
	unsigned char hi;
	unsigned char lo;
	unsigned int off;
	unsigned int base;

	fill_rect((unsigned int)sx << 3, (unsigned int)sy << 3,
		(unsigned int)BOARD_PIECE_WIDTH << 3,
		(unsigned int)BOARD_PIECE_HEIGHT << 3, paper);
	if(!src)
		return;
	for(cr = 0; cr < 3; ++cr)
	{
		base = ((unsigned int)(sy + cr) << 3) * BPL + ((unsigned int)sx << 2);
		for(row = 0; row < 8; ++row)
		{
			off = base + (unsigned int)row * BPL;
			for(cell = 0; cell < 4; ++cell)
			{
				b = src[(unsigned int)cr * 32 + (unsigned int)cell * 8 + row];
				for(k = 0; k < 4; ++k)
				{
					hi = (unsigned char)((b & 0x80) ? ink : paper);
					lo = (unsigned char)((b & 0x40) ? ink : paper);
					*fb_at(off + ((unsigned int)cell << 2) + k) =
						(unsigned char)((hi << 4) | lo);
					b <<= 2;
				}
			}
		}
	}
}

static char digit_or_hex(char n)
{
	if(n < 10)
		return (char)('0' + n);
	return (char)('A' + (n - 10));
}

/* POLCAT in Super Extended BASIC.  ROM is still mapped at $A000; drop
 * to 0.89 MHz for the call, then restore 1.79. */
static char polcat(void)
{
	char k;

	*(unsigned char *)0xFFD8 = 0;
	asm {
		jsr	[$A000]
		sta	:k
	}
	*(unsigned char *)0xFFD9 = 0;
	return k;
}

static char plat_TimeExpired(unsigned int ticks)
{
	if(*(unsigned char *)0xFF03 & 0x80)
	{
		(void)*(unsigned char *)0xFF02;
		++su_frames;
	}
	if(su_frames >= ticks)
	{
		su_frames = 0;
		return 1;
	}
	return 0;
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

static void puts_centered(char y, char *s, unsigned char fg, unsigned char bg)
{
	char n;

	n = (char)strlen(s);
	while(n && s[n - 1] == ' ')
		--n;
	put_span((char)((SCREEN_WIDTH - n) / 2), y, s, n, fg, bg);
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

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
	unsigned char i;
	unsigned char any;
	char got;

	gReturnToOS = 1;
	sc_rgb = 1;
	sc_bank = 255;
	subMenu = 0;
	su_frames = 0;
	memset(sc_prev, 0, 8);
	gime_init();

	puts_centered(SCREEN_HEIGHT / 2 - 1, gszAbout, COL_WHITE, COL_BG);
	puts_centered(SCREEN_HEIGHT / 2 + 1, (char *)"Color Computer 3, 2026.", COL_CYAN, COL_BG);
	blit_piece(SCREEN_WIDTH / 2 - 2, SCREEN_HEIGHT / 2 - 6, gfxTiles[KING - 1], COL_BLACK, COL_BG);
	blit_piece(SCREEN_WIDTH / 2 - 2, SCREEN_HEIGHT / 2 + 4, gfxTiles[KING - 1], COL_WHITE, COL_BG);
	puts_centered(22, (char *)"Press ENTER", COL_YELLOW, COL_BG);

	got = 0;
	any = 0;
	while(!got)
	{
		i = polcat();
		if(!i)
			any = 1;
		else if(any)
		{
			got = 1;
			if(i == 'C' || i == 'c')
			{
				sc_rgb = 0;
				apply_palette(0);
				*(unsigned char *)0xFF9A = pal_cmp[COL_BG];
			}
		}
	}
}

void plat_UpdateScreen(void)
{
}

char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller;
	int keyMask;
	char i, sx, sy, numMenuItems, maxLen;

	subMenu = 1;

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

	fill_rect((unsigned int)sx << 3, (unsigned int)sy << 3,
		((unsigned int)maxLen + 2) << 3, ((unsigned int)height + 2) << 3, COL_BLUE);
	fill_rect(((unsigned int)sx - 1) << 3, ((unsigned int)sy - 1) << 3,
		((unsigned int)maxLen + 4) << 3, 8, COL_YELLOW);
	fill_rect(((unsigned int)sx - 1) << 3, (unsigned int)sy << 3,
		8, ((unsigned int)height + 3) << 3, COL_YELLOW);
	fill_rect(((unsigned int)sx + maxLen + 2) << 3, (unsigned int)sy << 3,
		8, ((unsigned int)height + 3) << 3, COL_YELLOW);
	fill_rect((unsigned int)sx << 3, ((unsigned int)sy + height + 2) << 3,
		((unsigned int)maxLen + 2) << 3, 8, COL_YELLOW);

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

	subMenu = 0;
	if(clearLog)
		fill_rect((unsigned int)LOG_X << 3, 0, (unsigned int)LOG_W << 3,
			(unsigned int)SCREEN_HEIGHT << 3, COL_BG);

	fill_rect(0, 0, (unsigned int)(1 + 8 * BOARD_PIECE_WIDTH) << 3, 8, COL_BG);
	fill_rect(0, 0, 8, (unsigned int)SCREEN_HEIGHT << 3, COL_BG);

	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);

	for(i = 0; i < 8; ++i)
	{
		putch((char)(BOARD_X + i * BOARD_PIECE_WIDTH + 1), 0, (char)('A' + i), COL_BLACK, COL_BG);
		putch(0, (char)(BOARD_Y + i * BOARD_PIECE_HEIGHT + 1), (char)('8' - i), COL_BLACK, COL_BG);
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
	unsigned char ink;

	y = position / 8;
	x = position & 7;
	light = !((x & 1) ^ (y & 1));
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);
	paper = light ? COL_BG : COL_DGREEN;
	piece = gChessBoard[y][x];
	colour = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	ink = piece ? (unsigned char)(colour ? COL_WHITE : COL_BLACK) : paper;

	blit_piece(sx, sy, piece ? gfxTiles[piece - 1] : (const char *)0, ink, paper);

	if(gShowAttackBoard)
	{
		putch(sx, (char)(sy + 2),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][0]]),
			COL_RED, paper);
		putch((char)(sx + 3), (char)(sy + 2),
			digit_or_hex(gpAttackBoard[giAttackBoardOffset[position][1]]),
			COL_CYAN, paper);
		putch(sx, sy,
			digit_or_hex((char)(gChessBoard[y][x] & 0x0F)),
			COL_MAGENTA, paper);
		putch((char)(sx + 3), sy,
			digit_or_hex((char)(colour >> 7)),
			COL_YELLOW, paper);
	}
}

void plat_ShowSideToGoLabel(char side)
{
	unsigned char fg;

	fg = side ? COL_WHITE : COL_BLACK;
	put_span(LOG_X, 0, gszSideLabel[side], LOG_W, fg, COL_BG);
}

void plat_Highlight(char position, char color, char cursor)
{
	char y;
	char x;
	char sx;
	char sy;
	unsigned char bar;
	unsigned int px;
	unsigned int py;

	(void)cursor;
	y = position / 8;
	x = position & 7;
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);
	bar = sc_hcol[color & 7];
	px = (unsigned int)sx << 3;
	py = (unsigned int)sy << 3;
	fill_rect(px, py, 8, (unsigned int)BOARD_PIECE_HEIGHT << 3, bar);
	fill_rect(px + ((unsigned int)(BOARD_PIECE_WIDTH - 1) << 3), py,
		8, (unsigned int)BOARD_PIECE_HEIGHT << 3, bar);
}

void plat_ShowMessage(char *str, char color)
{
	unsigned char fg;
	unsigned char bg;

	fg = COL_YELLOW;
	bg = COL_BG;
	if(color == HCOLOR_INVALID)
	{
		fg = COL_WHITE;
		bg = COL_RED;
	}
	put_span(LOG_X, SCREEN_HEIGHT - 1, str, LOG_W, fg, bg);
}

void plat_ClearMessage(void)
{
	put_span(LOG_X, SCREEN_HEIGHT - 1, (char *)"", LOG_W, COL_WHITE, COL_BG);
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
			put_span(LOG_X, y, gLogStrBuffer, LOG_W, fg, COL_BG);
		}
		else
			put_span(LOG_X, y, (char *)"", LOG_W, COL_WHITE, COL_BG);
	}
}

void plat_AddToLogWinTop(void)
{
	plat_AddToLogWin();
}

int plat_ReadKeys(char blocking)
{
	char k;
	int keyMask;

	for(;;)
	{
		k = polcat();
		keyMask = 0;
		if(k && k != (char)sc_prev[0])
		{
			if(k == 94)
				keyMask |= INPUT_UP;
			else if(k == 10)
				keyMask |= INPUT_DOWN;
			else if(k == 8)
				keyMask |= INPUT_LEFT;
			else if(k == 9)
				keyMask |= INPUT_RIGHT;
			else if(k == 13)
				keyMask |= INPUT_SELECT;
			else if(k == 3)
				keyMask |= INPUT_BACKUP;
			else if(k == 'A' || k == 'a')
				keyMask |= INPUT_TOGGLE_A;
			else if(k == 'B' || k == 'b')
				keyMask |= INPUT_TOGGLE_B;
			else if(k == 'D' || k == 'd')
				keyMask |= INPUT_TOGGLE_D;
			else if(k == 'M' || k == 'm')
				keyMask |= INPUT_MENU;
			else if(k == 'U' || k == 'u')
				keyMask |= INPUT_UNDO;
			else if(k == 'R' || k == 'r')
				keyMask |= INPUT_REDO;
		}
		sc_prev[0] = (unsigned char)k;
		if(keyMask || !blocking)
			return keyMask;
	}
}

void plat_Shutdown(void)
{
	/* the framebuffer window is MMU slot 4 ($8000).  Super Extended BASIC
	 * lives there; jmp [$FFFE] without putting it back lands in video RAM
	 * and the machine sits on the last board draw. */
	*(unsigned char *)0xFFD8 = 0;
	*(unsigned char *)0xFFA4 = 0x3C;
	*(unsigned char *)0xFF90 = 0x80;
	asm {
		orcc	#$50
		clra
		sta	$0071
		ldy	$FFFE
		jmp	,y
	}
}

char plat_GetSeed(void)
{
	return (char)(su_frames ^ *(unsigned char *)0xFF00);
}
