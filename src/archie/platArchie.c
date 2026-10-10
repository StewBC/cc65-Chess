/*
 *	platArchie.c
 *	cc65 Chess
 *
 *	A3010, ARM250, RISC OS 3.11.  ArchieSDK, Mode 9, C64 layout.
 *	the 320x200 game sits between two 28-pixel green margins.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <kernel.h>
#include <swis.h>
#include <archie/video.h>
#include <archie/keyboard.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "../c64/dataC64.h"

extern const char sc_font[96][8];

#define TOP 28
#define STRIDE 160
#define GREEN 5
#define WHITE 1
#define BLACK 0
#define RED 2
#define CYAN 3
#define PURPLE 4
#define BLUE 6
#define YELLOW 7
#define GRAY1 11
#define GRAY2 12

/* C64 palette, rounded by the VIDC to its four bits per component.
 * Mode 9 lets each of these be set independently, unlike Mode 13. */
static const unsigned char sc_rgb[16][3] = {
	{0,0,0}, {255,255,255}, {136,0,0}, {170,255,238},
	{204,68,204}, {0,204,85}, {0,0,170}, {238,238,119},
	{221,136,85}, {102,68,0}, {255,119,119}, {51,51,51},
	{119,119,119}, {170,255,102}, {0,136,255}, {187,187,187}
};

static unsigned char *sc_screen;
/* full repaints are composed off screen so clearing a menu never exposes
 * blank coordinate margins while the pieces are being drawn */
static unsigned char sc_repaint[STRIDE * 256];
static int sc_oldMode = -1, sc_oldPointer = -1;
static char sc_inMenu, sc_splash, sc_pendingSelect, sc_haveMouse;
static int sc_mouseX, sc_mouseY;
static char sc_mouseTile = 255;
static int sc_menuX, sc_menuY, sc_menuWidth;
static char sc_menuCount, sc_menuHover;
static char sc_side = SIDE_WHITE;
static unsigned int sc_previousKeys;
static uint32_t sc_repeatTime, sc_pollTime;

static uint32_t ticks(void)
{
	_kernel_swi_regs r;
	if(_kernel_swi(OS_ReadMonotonicTime, &r, &r))
		return 0;
	return (uint32_t)r.r[0];
}

/* the left pixel is the low nibble.  every drawing coordinate is in
 * pixels, with y relative to the original C64 game area */
static void pixel(int x, int y, unsigned char colour)
{
	unsigned char *p;
	if(x < 0 || x >= 320 || y < -TOP || y >= 256 - TOP)
		return;
	p = sc_screen + (y + TOP) * STRIDE + x / 2;
	if(x & 1)
		*p = (unsigned char)((*p & 15) | (colour << 4));
	else
		*p = (unsigned char)((*p & 240) | colour);
}

static void fill(int x, int y, int w, int h, unsigned char colour)
{
	int row;
	/* all rectangles in this port start and end on byte boundaries */
	for(row = y; row < y + h; ++row)
		memset(sc_screen + (row + TOP) * STRIDE + x / 2,
			colour | (colour << 4), (size_t)(w / 2));
}

static void glyph(int x, int y, char ch, char fg, char bg)
{
	int row, bit;
	unsigned char bits;
	if(ch < 32 || ch > 127)
		ch = ' ';
	for(row = 0; row < 8; ++row)
	{
		bits = sc_font[ch - 32][row];
		for(bit = 0; bit < 8; ++bit)
			pixel(x + bit, y + row, bits & (128 >> bit) ? fg : bg);
	}
}

static void text(int x, int y, const char *s, int width, char fg, char bg)
{
	int i;
	char ch;
	for(i = 0; i < width; ++i)
	{
		ch = *s;
		if(ch)
			++s;
		glyph(x + i * 8, y, ch ? ch : ' ', fg, bg);
	}
}

static void centred(int y, const char *s, char fg)
{
	int n = (int)strlen(s);
	while(n && s[n - 1] == ' ')
		--n;
	text((320 - n * 8) / 2, y, s, n, fg, GREEN);
}

/* preserve the C64 XOR and outline/solid choice, including pieces on
 * squares of their own colour.  source bytes are in VIC cell order */
static void piece(int x, int y, char kind, char variant, char paper,
	char ink)
{
	int row, col, bit;
	unsigned char bits;
	for(row = 0; row < 24; ++row)
		for(col = 0; col < 4; ++col)
		{
			bits = gfxTiles[kind - 1][variant][(row / 8) * 32 + col * 8 + row % 8];
			for(bit = 0; bit < 8; ++bit)
				pixel(x + col * 8 + bit, y + row,
					bits & (128 >> bit) ? ink : paper);
		}
}

static char digit(char n)
{
	return (char)(n < 10 ? '0' + n : 'A' + n - 10);
}

static int scan_keyboard(uint32_t now)
{
	static const unsigned char keys[] = {
		KEY_UP, KEY_RIGHT, KEY_DOWN, KEY_LEFT, KEY_ESC, KEY_A,
		KEY_B, KEY_D, KEY_RETURN, KEY_M, KEY_U, KEY_R
	};
	unsigned int held = 0, pressed;
	int i;
	for(i = 0; i < 12; ++i)
		if(k_checkKeypress(keys[i]))
			held |= 1U << i;
	pressed = held & ~sc_previousKeys;
	if((held & INPUT_MOTION) != (sc_previousKeys & INPUT_MOTION))
		sc_repeatTime = now + 35;
	else if((held & INPUT_MOTION) && (int32_t)(now - sc_repeatTime) >= 0)
	{
		pressed |= held & INPUT_MOTION;
		sc_repeatTime = now + 8;
	}
	sc_previousKeys = held;
	/* Escape is handled as a key, not a CLI exception */
	if(held & INPUT_BACKUP)
		_kernel_osbyte(124, 0, 0);
	return (int)pressed;
}

static int scan_mouse(char blocking)
{
	_kernel_swi_regs r;
	int x, y;
	char moved, click, back, tile;
	static char previousButtons;
	if(_kernel_swi(OS_Mouse, &r, &r))
		return 0;
	x = r.r[0] / 4;
	y = 255 - r.r[1] / 4 - TOP;
	click = (char)((r.r[2] & 4) != 0);
	back = (char)((r.r[2] & 1) && !(previousButtons & 1));
	moved = (char)(sc_haveMouse && (x != sc_mouseX || y != sc_mouseY));
	sc_haveMouse = 1;
	sc_mouseX = x;
	sc_mouseY = y;
	if(click && (previousButtons & 4))
		click = 0;
	previousButtons = (char)r.r[2];
	if(back)
	{
		sc_pendingSelect = 0;
		gGotoTile = 255;
		return INPUT_BACKUP;
	}
	if(sc_splash)
		return click ? INPUT_SELECT : 0;
	if(sc_inMenu)
	{
		if(x >= sc_menuX && x < sc_menuX + sc_menuWidth &&
			y >= sc_menuY + 16 && y < sc_menuY + (sc_menuCount + 1) * 8)
		{
			tile = (char)((y - sc_menuY) / 8 - 1);
			if(moved || click)
				sc_menuHover = tile;
			if(click)
				return INPUT_SELECT;
		}
		return 0;
	}
	/* nonblocking reads also happen in the search and between turns.
 * only a human's blocking read may jump the board cursor */
	if(!blocking || x < 8 || x >= 264 || y < 8 || y >= 200)
		return 0;
	tile = (char)(((y - 8) / 24) * 8 + (x - 8) / 32);
	if(click || (moved && tile != sc_mouseTile))
	{
		sc_mouseTile = tile;
		gGotoTile = tile;
		sc_pendingSelect = click;
		/* human_Play handles movement before selection.  deliver the
 * click on the next call, after it has computed the new square */
		return INPUT_UP;
	}
	return 0;
}

int plat_ReadKeys(char blocking)
{
	uint32_t now;
	int keys, mouse;
	if(sc_pendingSelect)
	{
		sc_pendingSelect = 0;
		return INPUT_SELECT;
	}
	for(;;)
	{
		now = ticks();
		/* search polls every 64 nodes; keyboard SWIs need only run
 * once a centisecond.  never wait for VSync inside the search */
		if(now != sc_pollTime || blocking)
		{
			sc_pollTime = now;
			keys = scan_keyboard(now);
			mouse = scan_mouse(blocking);
			if(keys & (INPUT_MENU | INPUT_BACKUP))
			{
				sc_pendingSelect = 0;
				gGotoTile = 255;
				return keys & (INPUT_MENU | INPUT_BACKUP);
			}
			if(mouse)
				return mouse;
			if(keys)
			{
				/* keyboard navigation changes which square is current.
 * the next real mouse movement must be allowed to reclaim it */
				sc_mouseTile = 255;
				return keys;
			}
		}
		if(!blocking)
			return 0;
		v_waitForVSync();
	}
}

void plat_Init(void)
{
	_kernel_swi_regs r;
	int i, j;
	r.r[0] = 135;
	r.r[1] = r.r[2] = 0;
	if(!_kernel_swi(OS_Byte, &r, &r))
		sc_oldMode = r.r[2];
	r.r[0] = 106;
	r.r[1] = 1;
	r.r[2] = 0;
	if(!_kernel_swi(OS_Byte, &r, &r))
		sc_oldPointer = r.r[1];
	atexit(plat_Shutdown);
	v_setMode(9);
	v_disableTextCursor();
	/* a mode change resets the pointer selection.  select it again
 * after switching, so mouse play also has a visible pointer */
	_kernel_osbyte(106, 1, 0);
	sc_screen = (unsigned char *)v_getScreenAddress();
	if(!sc_screen)
	{
		plat_Shutdown();
		fputs("Chess: no screen memory\n", stderr);
		exit(1);
	}
	for(i = 0; i < 16; ++i)
	{
		_kernel_oswrch(19);
		_kernel_oswrch(i);
		_kernel_oswrch(16);
		for(j = 0; j < 3; ++j)
			_kernel_oswrch(sc_rgb[i][j]);
	}
	v_setBorderColourRGB(sc_rgb[GREEN][0], sc_rgb[GREEN][1], sc_rgb[GREEN][2]);
	memset(sc_screen, GREEN | (GREEN << 4), STRIDE * 256);
	gReturnToOS = 1;
	sc_splash = 1;
	centred(88, gszAbout, WHITE);
	centred(104, "Acorn Archimedes, 2026.", BLACK);
	piece(144, 48, KING, 1, GREEN, BLACK);
	piece(144, 144, KING, 1, GREEN, WHITE);
	centred(184, "Press RETURN or click", YELLOW);
	plat_ReadKeys(1);
	sc_splash = 0;
}

void plat_UpdateScreen(void)
{
}

char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller;
	char count, maxLen, len, i, n, painted;
	int x, y, keys, width;
	uint32_t last;
	char buf[41], *p;
	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
	}
	maxLen = 0;
	for(count = 0; menuItems[count]; ++count)
	{
		len = (char)strlen(menuItems[count]);
		if(len > maxLen)
			maxLen = len;
	}
	if(maxLen > 36)
		maxLen = 36;
	width = (maxLen + 2) * 8;
	x = (320 - width) / 2;
	y = (200 - (height + 3) * 8) / 2;
	sc_inMenu = 1;
	sc_pendingSelect = 0;
	sc_menuX = x;
	sc_menuY = y;
	sc_menuWidth = width;
	sc_menuCount = count;
	sc_menuHover = 0;
	fill(x - 8, y - 8, width + 16, (height + 4) * 8, YELLOW);
	fill(x, y, width, (height + 2) * 8, BLUE);
	text(x + 8, y, menuItems[0], maxLen, YELLOW, BLUE);
	i = 1;
	painted = 0;
	last = ticks() - 16;
	for(;;)
	{
		if(painted != i)
		{
			for(n = 1; n < count; ++n)
			{
				/* after the initial paint only the old and new selection
				 * change.  overwrite their pixels without blanking first */
				if(painted && n != painted && n != i)
					continue;
				text(x + 8, y + (n + 1) * 8, menuItems[n], maxLen,
					n == i ? WHITE : GRAY2, BLUE);
				glyph(x, y + (n + 1) * 8, n == i ? '>' : ' ', WHITE, BLUE);
				glyph(x + width - 8, y + (n + 1) * 8,
					n == i ? '<' : ' ', WHITE, BLUE);
			}
			painted = i;
		}
		if(ticks() - last >= 16 || last == 0)
		{
			p = pScroller;
			for(n = 0; n < maxLen + 2; ++n)
			{
				if(!*p)
					p = scroller;
				buf[n] = *p ? *p++ : ' ';
			}
			buf[n] = 0;
			text(x, y + (height + 1) * 8, buf, maxLen + 2, CYAN, BLUE);
			last = ticks();
			if(*pScroller)
				++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
		keys = plat_ReadKeys(0);
		if(sc_menuHover)
		{
			i = sc_menuHover;
			sc_menuHover = 0;
		}
		if(keys & INPUT_UP)
			i = i == 1 ? count - 1 : i - 1;
		else if(keys & INPUT_DOWN)
			i = i == count - 1 ? 1 : i + 1;
		if(keys & (INPUT_SELECT | INPUT_BACKUP))
			break;
		v_waitForVSync();
	}
	sc_inMenu = 0;
	sc_mouseTile = 255;
	return keys & INPUT_BACKUP ? 0 : i;
}

void plat_DrawBoard(char clearLog)
{
	char i;
	char tile[2], pieces[2], colors[2], outcome;
	unsigned char *visible = sc_screen;
	memcpy(sc_repaint, visible, sizeof(sc_repaint));
	sc_screen = sc_repaint;
	sc_inMenu = 0;
	fill(0, 0, 264, 8, GREEN);
	fill(0, 0, 8, 200, GREEN);
	/* menus overlap the log as well as the board */
	fill(264, 0, 56, 200, GREEN);
	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);
	for(i = 0; i < 8; ++i)
	{
		glyph(16 + i * 32, 0, 'A' + i, BLACK, GREEN);
		glyph(0, 16 + i * 24, '8' - i, BLACK, GREEN);
	}
	if(!clearLog)
	{
		/* repainting is not a log operation.  preserve the globals
 * around the ring walk, just as the Mac window repaint does */
		memcpy(tile, gTile, 2);
		memcpy(pieces, gPiece, 2);
		memcpy(colors, gColor, 2);
		outcome = gOutcome;
		plat_AddToLogWin();
		memcpy(gTile, tile, 2);
		memcpy(gPiece, pieces, 2);
		memcpy(gColor, colors, 2);
		gOutcome = outcome;
	}
	plat_ShowSideToGoLabel(sc_side);
	sc_screen = visible;
	v_waitForVSync();
	memcpy(visible, sc_repaint, sizeof(sc_repaint));
}

void plat_DrawSquare(char position)
{
	char y = position / 8, x = position & 7;
	char light = !((x & 1) ^ (y & 1));
	char p = gChessBoard[y][x], white = (p & PIECE_WHITE) != 0;
	char kind = p & PIECE_DATA;
	int px = 8 + x * 32, py = 8 + y * 24;
	char paper = light ? WHITE : BLACK;
	fill(px, py, 32, 24, paper);
	if(kind)
		piece(px, py, kind, white == light ? 0 : 1, paper,
			light ? BLACK : WHITE);
	if(gShowAttackBoard)
	{
		glyph(px, py + 16, digit(gpAttackBoard[giAttackBoardOffset[position][0]]),
			white ? RED : GREEN, BLACK);
		glyph(px + 24, py + 16, digit(gpAttackBoard[giAttackBoardOffset[position][1]]),
			white ? GREEN : RED, BLACK);
		glyph(px, py, digit(p & 15), PURPLE, BLACK);
		glyph(px + 24, py, digit(white), white ? WHITE : GRAY1, BLACK);
	}
}

void plat_ShowSideToGoLabel(char side)
{
	sc_side = side;
	text(272, 0, gszSideLabel[side], 6, side ? WHITE : BLACK, GREEN);
}

void plat_Highlight(char position, char color, char cursor)
{
	int x = 8 + (position & 7) * 32, y = 8 + (position / 8) * 24;
	int row, col, px;
	char light = !(((position & 7) & 1) ^ ((position / 8) & 1));
	unsigned char *p, byte, fg, bg;
	(void)cursor;
	plat_DrawSquare(position);
	/* recolour the two outer character cells, as the VIC does.  keep
 * the piece/number pixels rather than painting over their shapes */
	fg = light ? color : BLACK;
	bg = light ? BLACK : color;
	for(row = y; row < y + 24; ++row)
		for(col = 0; col < 8; col += 2)
		{
			for(px = x + col; px <= x + col + 24; px += 24)
			{
				p = sc_screen + (row + TOP) * STRIDE + px / 2;
				byte = *p;
				*p = (unsigned char)(((byte & 15) ? fg : bg) |
					((byte & 240) ? fg << 4 : bg << 4));
			}
		}
}

void plat_ShowMessage(char *str, char color)
{
	(void)color;
	text(264, 192, str, 7, RED, GREEN);
}

void plat_ClearMessage(void)
{
	text(264, 192, "", 7, RED, GREEN);
}

void plat_AddToLogWin(void)
{
	char y;
	for(y = 1; y <= 22; ++y)
	{
		if(undo_FindUndoLine(22 - y))
		{
			frontend_FormatLogString();
			text(272, y * 8, gLogStrBuffer, 6, gColor[0] ? WHITE : BLACK, GREEN);
		}
		else
			text(272, y * 8, "", 6, WHITE, GREEN);
	}
}

void plat_AddToLogWinTop(void)
{
	plat_AddToLogWin();
}

char plat_GetSeed(void)
{
	return (char)ticks();
}

void plat_Shutdown(void)
{
	_kernel_swi_regs r;
	if(sc_oldMode < 0)
		return;
	v_setMode((unsigned)sc_oldMode);
	sc_oldMode = -1;
	if(sc_oldPointer >= 0)
	{
		r.r[0] = 106;
		r.r[1] = sc_oldPointer;
		r.r[2] = 0;
		_kernel_swi(OS_Byte, &r, &r);
	}
	_kernel_osbyte(21, 0, 0);
	_kernel_osbyte(124, 0, 0);
	sc_screen = 0;
}
