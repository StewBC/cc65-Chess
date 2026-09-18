/*
 *	platAgon.c
 *	cc65 Chess
 *
 *	Agon Light / Light 2 / Console8.  C64 layout on VDP mode 12:
 *	320x200, 64 colours, 40x25 characters, 4x3 squares, 32x24 pieces,
 *	green field, log in the seven columns that remain.
 *
 *	the VDP is a command stream, not a framebuffer — squares are filled
 *	rectangles, pieces are bitmaps uploaded once at init.
 */

#include <string.h>
#include <stdint.h>
#include <agon/vdp.h>
#include <agon/mos.h>
#include <agon/keyboard.h>
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

#define COL_FIELD				GREEN
#define COL_DARK				GREEN
#define COL_LIGHT				BRIGHT_GREEN
#define COL_BLACK				BLACK
#define COL_WHITE				WHITE
#define COL_YELLOW				YELLOW
#define COL_BLUE				BLUE
#define COL_CYAN				CYAN
#define COL_RED					RED
#define COL_MAGENTA				MAGENTA
#define COL_GRAY				BRIGHT_BLACK

/* HCOLOR_* as bar colours.  C64 uses the VIC numbers (empty is purple,
 * valid is green, attack is cyan).  green bars vanish on the dark
 * squares, so empty is magenta and valid/attack use bright cyan. */
static const char sc_hcol[] =
{
	COL_BLACK,
	COL_WHITE,
	COL_RED,		/* invalid / black */
	BRIGHT_CYAN,		/* attack — destination */
	MAGENTA,		/* empty / enemy */
	BRIGHT_CYAN,		/* valid — own piece with moves */
	COL_YELLOW,		/* selected */
	COL_BLUE
};

static uint32_t su_time;
static char textStr[41];

/* FabGL VirtualKey values for the arrows / enter / esc.  counted from
 * fdivitto's enum: after PAGEUP/PAGEDOWN come UP, KP_UP, DOWN, ... */
#define VK_RETURN		143
#define VK_KP_ENTER		144
#define VK_ESCAPE		125
#define VK_UP			150
#define VK_KP_UP		151
#define VK_DOWN			152
#define VK_KP_DOWN		153
#define VK_LEFT			154
#define VK_KP_LEFT		155
#define VK_RIGHT		156
#define VK_KP_RIGHT		157

/*-----------------------------------------------------------------------*/
static void fill_rect(int x, int y, int w, int h, unsigned char colour)
{
	if(w < 1 || h < 1)
		return;
	vdp_set_graphics_fg_colour(0, colour);
	vdp_filled_rectangle(x, y, x + w - 1, y + h - 1);
}

static void put_span(char x, char y, char *s, char n, unsigned char fg, unsigned char bg)
{
	char i;
	char c;

	vdp_write_at_text_cursor();
	vdp_set_text_colour(fg);
	vdp_set_text_bg_colour(bg);
	vdp_cursor_tab(x, y);
	for(i = 0; i < n; ++i)
	{
		c = (s && s[i]) ? s[i] : 0;
		if(!c)
			break;
		if(c < 32 || c > 126)
			c = 32;
		putch(c);
	}
	for(; i < n; ++i)
		putch(' ');
	/* writing into column 39 leaves a pending newline; without scroll
	 * protection that shoves the whole screen up when the AI prints
	 * "Think" on the last row.  park the cursor so it cannot fire. */
	vdp_cursor_tab(0, 0);
}

static void putch_xy(char x, char y, char ch, unsigned char fg, unsigned char bg)
{
	textStr[0] = ch;
	textStr[1] = 0;
	put_span(x, y, textStr, 1, fg, bg);
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

/* 32x24 1-bit C64 cell order → RGBA8888.  colour 0 black, 1 white. */
static void load_piece_bitmaps(void)
{
	static unsigned char rgba[32 * 24 * 4];
	char p;
	char colour;
	char cr;
	char row;
	char cell;
	char bit;
	const char *src;
	unsigned char *dst;
	unsigned char b;
	unsigned char on;

	for(p = 0; p < PAWN; ++p)
	{
		src = gfxTiles[p];
		for(colour = 0; colour < 2; ++colour)
		{
			dst = rgba;
			for(cr = 0; cr < 3; ++cr)
			{
				for(row = 0; row < 8; ++row)
				{
					for(cell = 0; cell < 4; ++cell)
					{
						b = src[(unsigned)cr * 32 + (unsigned)cell * 8 + row];
						for(bit = 0; bit < 8; ++bit)
						{
							on = (unsigned char)(b & (0x80 >> bit));
							if(on)
							{
								if(colour)
								{
									*dst++ = 0xFF;
									*dst++ = 0xFF;
									*dst++ = 0xFF;
									*dst++ = 0xFF;
								}
								else
								{
									*dst++ = 0x00;
									*dst++ = 0x00;
									*dst++ = 0x00;
									*dst++ = 0xFF;
								}
							}
							else
							{
								*dst++ = 0x00;
								*dst++ = 0x00;
								*dst++ = 0x00;
								*dst++ = 0x00;
							}
						}
					}
				}
			}
			vdp_select_bitmap(p + colour * PAWN);
			vdp_load_bitmap(32, 24, rgba);
		}
	}
}

static void blit_piece(char sx, char sy, char piece, char white)
{
	int px;
	int py;

	px = (int)sx << 3;
	py = (int)sy << 3;
	if(!piece)
		return;
	vdp_select_bitmap((piece - 1) + (white ? PAWN : 0));
	vdp_draw_bitmap(px, py);
}

static char plat_TimeExpired(unsigned int cs)
{
	uint32_t now;

	now = getsysvar_time();
	if((now - su_time) >= cs)
	{
		su_time = now;
		return 1;
	}
	return 0;
}

static int map_ascii(unsigned char k)
{
	if(k == 11 || k == 30 || k == 139)
		return INPUT_UP;
	if(k == 10 || k == 31 || k == 138)
		return INPUT_DOWN;
	if(k == 8 || k == 28 || k == 136)
		return INPUT_LEFT;
	if(k == 9 || k == 21 || k == 29 || k == 137)
		return INPUT_RIGHT;
	if(k == 13)
		return INPUT_SELECT;
	if(k == 27)
		return INPUT_BACKUP;
	if(k == 'A' || k == 'a')
		return INPUT_TOGGLE_A;
	if(k == 'B' || k == 'b')
		return INPUT_TOGGLE_B;
	if(k == 'D' || k == 'd')
		return INPUT_TOGGLE_D;
	if(k == 'M' || k == 'm')
		return INPUT_MENU;
	if(k == 'U' || k == 'u')
		return INPUT_UNDO;
	if(k == 'R' || k == 'r')
		return INPUT_REDO;
	return 0;
}

static int map_vkey(unsigned char v)
{
	if(v == VK_UP || v == VK_KP_UP)
		return INPUT_UP;
	if(v == VK_DOWN || v == VK_KP_DOWN)
		return INPUT_DOWN;
	if(v == VK_LEFT || v == VK_KP_LEFT)
		return INPUT_LEFT;
	if(v == VK_RIGHT || v == VK_KP_RIGHT)
		return INPUT_RIGHT;
	if(v == VK_RETURN || v == VK_KP_ENTER)
		return INPUT_SELECT;
	if(v == VK_ESCAPE)
		return INPUT_BACKUP;
	return 0;
}

static int scan_keys(void)
{
	int keyMask;
	struct keyboard_event_t e;

	keyMask = 0;
	while(kbuf_poll_event(&e))
	{
		if(!e.isdown)
			continue;
		keyMask |= map_ascii(e.ascii);
		keyMask |= map_vkey(e.vkey);
	}
	return keyMask;
}

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
	gReturnToOS = 1;
	su_time = getsysvar_time();

	vdp_mode(12);
	waitvblank();
	vdp_set_pixel_coordinates();
	vdp_cursor_enable(false);
	vdp_page_mode_off();
	/* bit 0: pending newline instead of an immediate scroll when a
	 * character is printed in the last column.  VDP 2.7.0+. */
	vdp_cursor_behaviour(1, 1);
	vdp_reset_sprites();
	kbuf_init(16);
	vdp_set_text_colour(COL_WHITE);
	vdp_set_text_bg_colour(COL_FIELD);
	vdp_clear_screen();
	fill_rect(0, 0, 320, 200, COL_FIELD);

	load_piece_bitmaps();

	puts_centered(SCREEN_HEIGHT / 2 - 1, gszAbout, COL_WHITE, COL_FIELD);
	puts_centered(SCREEN_HEIGHT / 2 + 1, (char *)"Agon Light, 2026.", COL_CYAN, COL_FIELD);
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
	char y;
	char x;
	char sx;
	char sy;
	unsigned char bar;
	int px;
	int py;

	(void)cursor;
	y = position / 8;
	x = position & 7;
	sx = (char)(BOARD_X + x * BOARD_PIECE_WIDTH);
	sy = (char)(BOARD_Y + y * BOARD_PIECE_HEIGHT);
	bar = sc_hcol[color & 7];
	px = (int)sx << 3;
	py = (int)sy << 3;
	fill_rect(px, py, 8, BOARD_PIECE_HEIGHT << 3, bar);
	fill_rect(px + ((BOARD_PIECE_WIDTH - 1) << 3), py,
		8, BOARD_PIECE_HEIGHT << 3, bar);
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
	return (char)getsysvar_time();
}

void plat_Shutdown(void)
{
	kbuf_deinit();
	vdp_cursor_enable(true);
	vdp_set_text_colour(COL_WHITE);
	vdp_set_text_bg_colour(COL_BLACK);
	vdp_mode(1);
}
