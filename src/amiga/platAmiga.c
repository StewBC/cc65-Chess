/*
 *	platAmiga.c
 *	cc65 Chess
 *
 *	A500, 68000, Kickstart 1.3.  Lowres 320x256, four bitplanes, C64 layout.
 *	The game is drawn into a chunky buffer.  A full frame is converted into
 *	the hidden planes and the screen is then pointed at them, so it arrives
 *	at once.  A small change is copied onto the visible planes; one text
 *	row is small enough that the copy does not streak.  Pieces are the C64
 *	bitmaps.
 *	Escape is Backup.  The mouse moves the cursor and the left button is
 *	Return; the right button is Backup, the same split the Archimedes uses.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>

#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "../c64/dataC64.h"
#include "mem.h"

extern const char sc_font[96][8];
extern void amiga_fail(const char *s);
extern void c2p_span(const unsigned char *src, unsigned char *dst,
	int groups, int rows, const unsigned long *lut);

#define TOP 28
#define STRIDE 160
#define PLANESZ (40 * 256)
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

/* C64 colour numbers, packed as Amiga 0x0RGB.  The Archimedes port uses
 * the same indices, so a highlight colour from human.c lands on the same
 * ink it does there. */
static const unsigned short sc_rgb[16] = {
	0x000, 0xFFF, 0x800, 0xAFE,
	0xC4C, 0x0C5, 0x00A, 0xEE7,
	0xD85, 0x640, 0xF77, 0x333,
	0x777, 0xAF6, 0x08F, 0xBBB
};

/* raw key order matches the INPUT_ bit positions */
static const unsigned char sc_raw[12] = {
	0x4C, 0x4E, 0x4D, 0x4F, 0x45, 0x20,
	0x35, 0x22, 0x44, 0x37, 0x16, 0x13
};

static unsigned char sc_chunky[STRIDE * 256];
/* four shifts of "two pixels -> the bit pair they own on each plane" */
static unsigned long sc_c2p[4][256];
static unsigned char *sc_chip, *sc_shown, *sc_back;
static struct BitMap sc_bm;
static struct Screen *sc_screen;
static struct Window *sc_win;
static char sc_defer;
static char sc_inMenu, sc_splash, sc_pendingSelect, sc_haveMouse;
static char sc_click, sc_right;
static int sc_mouseX, sc_mouseY;
static char sc_mouseTile = 255;
static int sc_menuX, sc_menuY, sc_menuWidth;
static char sc_menuCount, sc_menuHover;
static char sc_side = SIDE_WHITE;
static unsigned int sc_held, sc_previous;
static unsigned long sc_frame, sc_repeatAt;
static unsigned short sc_prevV;

/*-----------------------------------------------------------------------*/
static unsigned short vpos(void)
{
	unsigned short hi, lo;

	hi = *(volatile unsigned short *)0xDFF004;
	lo = *(volatile unsigned short *)0xDFF006;
	return (unsigned short)(((hi & 1) << 8) | (lo >> 8));
}

static unsigned long frames(void)
{
	unsigned short v;

	v = vpos();
	if(v < sc_prevV)
		++sc_frame;
	sc_prevV = v;
	return sc_frame;
}

/* one long per source byte: plane 0 in the high byte, already shifted
 * onto the bit pair that byte owns.  c2p.s ORs four of these together. */
static void c2p_init(void)
{
	unsigned int b, shift, p;
	unsigned char pix0, pix1, bits;
	unsigned long pack;

	for(b = 0; b < 256; ++b)
	{
		pix0 = (unsigned char)(b & 15);
		pix1 = (unsigned char)(b >> 4);
		for(shift = 0; shift < 4; ++shift)
		{
			pack = 0;
			for(p = 0; p < 4; ++p)
			{
				bits = 0;
				if(pix0 & (unsigned char)(1 << p))
					bits |= 0x80;
				if(pix1 & (unsigned char)(1 << p))
					bits |= 0x40;
				bits >>= shift * 2;
				pack = (pack << 8) | bits;
			}
			sc_c2p[shift][b] = pack;
		}
	}
}

static void point_at(unsigned char *planes)
{
	struct BitMap *bm;
	int i;

	bm = &sc_screen->BitMap;
	if(sc_screen->RastPort.BitMap)
		bm = sc_screen->RastPort.BitMap;
	for(i = 0; i < 4; ++i)
	{
		bm->Planes[i] = planes + (long)i * PLANESZ;
		sc_screen->BitMap.Planes[i] = bm->Planes[i];
		sc_bm.Planes[i] = bm->Planes[i];
	}
	if(sc_screen->ViewPort.RasInfo && sc_screen->ViewPort.RasInfo->BitMap
		&& sc_screen->ViewPort.RasInfo->BitMap != bm)
	{
		bm = sc_screen->ViewPort.RasInfo->BitMap;
		for(i = 0; i < 4; ++i)
			bm->Planes[i] = planes + (long)i * PLANESZ;
	}
	WaitTOF();
	MakeScreen(sc_screen);
	RethinkDisplay();
}

static void present(int x, int y, int w, int h)
{
	int x0, x1;

	if(!sc_shown)
		return;
	if(x < 0)
	{
		w += x;
		x = 0;
	}
	if(y < 0)
	{
		h += y;
		y = 0;
	}
	if(x + w > 320)
		w = 320 - x;
	if(y + h > 256)
		h = 256 - y;
	if(w <= 0 || h <= 0)
		return;
	x0 = x & ~7;
	x1 = (x + w + 7) & ~7;
	if(x1 > 320)
		x1 = 320;
	c2p_span(sc_chunky + y * STRIDE + x0 / 2,
		sc_shown + y * 40 + x0 / 8,
		(x1 - x0) >> 3, h, sc_c2p[0]);
}

/* the whole frame, into the hidden planes, then the copper follows.
 * before the screen exists the shown planes are filled in place. */
static void present_all(void)
{
	unsigned char *target, *tmp;

	target = sc_screen ? sc_back : sc_shown;
	c2p_span(sc_chunky, target, 40, 256, sc_c2p[0]);
	if(!sc_screen)
		return;
	tmp = sc_shown;
	sc_shown = sc_back;
	sc_back = tmp;
	point_at(sc_shown);
}

/* drawing talks in C64 game coordinates.  TOP is the green margin above. */
static void present_game(int x, int y, int w, int h)
{
	if(!sc_defer)
		present(x, y + TOP, w, h);
}

static void pixel(int x, int y, unsigned char colour)
{
	unsigned char *p;

	y += TOP;
	if(x < 0 || x >= 320 || y < 0 || y >= 256)
		return;
	p = sc_chunky + y * STRIDE + x / 2;
	if(x & 1)
		*p = (unsigned char)((*p & 15) | (colour << 4));
	else
		*p = (unsigned char)((*p & 240) | colour);
}

static void fill(int x, int y, int w, int h, unsigned char colour)
{
	int row;
	unsigned char both;

	both = (unsigned char)(colour | (colour << 4));
	for(row = y; row < y + h; ++row)
		memset(sc_chunky + (row + TOP) * STRIDE + x / 2, both, (unsigned long)(w / 2));
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
	int n;

	n = (int)strlen(s);
	while(n && s[n - 1] == ' ')
		--n;
	text((320 - n * 8) / 2, y, s, n, fg, GREEN);
}

/* eight pixels on a byte boundary.  bit 7 is the left-hand pixel. */
static void span8(int x, int y, unsigned char bits,
	unsigned char paper, unsigned char ink)
{
	unsigned char *p;
	unsigned char i, left, right;

	y += TOP;
	p = sc_chunky + y * STRIDE + x / 2;
	for(i = 0; i < 4; ++i)
	{
		left = (bits & 0x80) ? ink : paper;
		right = (bits & 0x40) ? ink : paper;
		*p++ = (unsigned char)(left | (right << 4));
		bits <<= 2;
	}
}

/* source bytes are in VIC cell order: four columns of eight, three rows */
static void piece(int x, int y, char kind, char variant, char paper, char ink)
{
	int row, col;
	unsigned char bits;

	for(row = 0; row < 24; ++row)
		for(col = 0; col < 4; ++col)
		{
			bits = gfxTiles[kind - 1][variant][(row / 8) * 32 + col * 8 + row % 8];
			span8(x + col * 8, y + row, bits, paper, ink);
		}
}

static char digit(char n)
{
	return (char)(n < 10 ? '0' + n : 'A' + n - 10);
}

static unsigned int bit_for(unsigned short code)
{
	unsigned char i;

	code = (unsigned short)(code & 0x7F);
	for(i = 0; i < 12; ++i)
		if(sc_raw[i] == (unsigned char)code)
			return 1U << i;
	return 0;
}

static void drain(void)
{
	struct IntuiMessage *msg;
	unsigned short code;

	if(!sc_win)
		return;
	while((msg = (struct IntuiMessage *)GetMsg(sc_win->UserPort)))
	{
		if(msg->Class == IDCMP_RAWKEY)
		{
			code = msg->Code;
			if(code & IECODE_UP_PREFIX)
				sc_held &= ~bit_for(code);
			else
				sc_held |= bit_for(code);
		}
		else if(msg->Class == IDCMP_MOUSEBUTTONS)
		{
			if(msg->Code == SELECTDOWN)
				sc_click = 1;
			else if(msg->Code == MENUDOWN)
				sc_right = 1;
		}
		ReplyMsg((struct Message *)msg);
	}
}

static int scan_keys(unsigned long now)
{
	unsigned int pressed;

	pressed = sc_held & ~sc_previous;
	if((sc_held & INPUT_MOTION) != (sc_previous & INPUT_MOTION))
		sc_repeatAt = now + 18;
	else if((sc_held & INPUT_MOTION) && now >= sc_repeatAt)
	{
		pressed |= sc_held & INPUT_MOTION;
		sc_repeatAt = now + 4;
	}
	sc_previous = sc_held;
	return (int)pressed;
}

static int scan_mouse(char blocking)
{
	int x, y;
	char moved, click, tile;

	if(!sc_win)
		return 0;
	x = sc_win->MouseX;
	y = sc_win->MouseY - TOP;
	click = sc_click;
	sc_click = 0;
	moved = (char)(sc_haveMouse && (x != sc_mouseX || y != sc_mouseY));
	sc_haveMouse = 1;
	sc_mouseX = x;
	sc_mouseY = y;
	if(sc_right)
	{
		sc_right = 0;
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
	/* a nonblocking read is the search asking whether to stop.  only a
	 * human's blocking read may jump the board cursor */
	if(!blocking || x < 8 || x >= 264 || y < 8 || y >= 200)
		return 0;
	tile = (char)(((y - 8) / 24) * 8 + (x - 8) / 32);
	if(click || (moved && tile != sc_mouseTile))
	{
		sc_mouseTile = tile;
		gGotoTile = tile;
		sc_pendingSelect = click;
		return INPUT_UP;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
int plat_ReadKeys(char blocking)
{
	unsigned long now;
	int keys, mouse;

	if(sc_pendingSelect)
	{
		sc_pendingSelect = 0;
		return INPUT_SELECT;
	}
	for(;;)
	{
		drain();
		now = frames();
		keys = scan_keys(now);
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
			sc_mouseTile = 255;
			return keys;
		}
		if(!blocking)
			return 0;
		WaitTOF();
	}
}

void plat_Init(void)
{
	struct NewScreen ns;
	struct NewWindow nw;

	/* MEMF_CLEAR waits on the blitter.  present() overwrites the planes.
	 * two buffers: the one on show, and the one a full frame is built in. */
	sc_chip = AllocMem(PLANESZ * 4 * 2, MEMF_CHIP);
	if(!sc_chip)
		amiga_fail("Chess: no chip memory for the screen.\n");
	sc_shown = sc_chip;
	sc_back = sc_chip + PLANESZ * 4;
	c2p_init();
	InitBitMap(&sc_bm, 4, 320, 256);
	sc_bm.Flags = BMF_DISPLAYABLE;
	sc_bm.Planes[0] = sc_shown;
	sc_bm.Planes[1] = sc_shown + PLANESZ;
	sc_bm.Planes[2] = sc_shown + PLANESZ * 2;
	sc_bm.Planes[3] = sc_shown + PLANESZ * 3;

	/* OpenScreen clears the custom bitmap.  The picture is converted
	 * into the other buffer, and the screen is pointed at that once
	 * it exists, so the title arrives whole. */
	gReturnToOS = 1;
	memset(sc_chunky, GREEN | (GREEN << 4), sizeof(sc_chunky));
	sc_splash = 1;
	centred(88, gszAbout, WHITE);
	centred(104, "Amiga 500, 2026.", BLACK);
	piece(144, 48, KING, 1, GREEN, BLACK);
	piece(144, 144, KING, 1, GREEN, WHITE);
	centred(184, "Press RETURN or click", YELLOW);
	c2p_span(sc_chunky, sc_back, 40, 256, sc_c2p[0]);

	memset(&ns, 0, sizeof(ns));
	ns.Width = 320;
	ns.Height = 256;
	ns.Depth = 4;
	/* SCREENQUIET is a later Kickstart flag.  1.3 just gets a custom
	 * screen; the title bar is hidden by the green margin. */
	ns.Type = CUSTOMSCREEN | CUSTOMBITMAP;
	ns.CustomBitMap = &sc_bm;
	sc_screen = OpenScreen(&ns);
	if(!sc_screen)
	{
		FreeMem(sc_chip, PLANESZ * 4 * 2);
		sc_chip = sc_shown = sc_back = 0;
		amiga_fail("Chess: the screen did not open.\n");
	}
	LoadRGB4(&sc_screen->ViewPort, sc_rgb, 16);

	memset(&nw, 0, sizeof(nw));
	nw.Width = 320;
	nw.Height = 256;
	nw.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS;
	nw.Flags = WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP | WFLG_NOCAREREFRESH;
	nw.Screen = sc_screen;
	nw.Type = CUSTOMSCREEN;
	nw.MinWidth = nw.MaxWidth = 320;
	nw.MinHeight = nw.MaxHeight = 256;
	sc_win = OpenWindow(&nw);
	if(!sc_win)
	{
		plat_Shutdown();
		amiga_fail("Chess: the window did not open.\n");
	}

	/* the cleared buffer is on show.  swap in the finished one. */
	{
		unsigned char *tmp;

		tmp = sc_shown;
		sc_shown = sc_back;
		sc_back = tmp;
	}
	point_at(sc_shown);
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
	unsigned long last;
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
	for(n = 1; n < count; ++n)
	{
		text(x + 8, y + (n + 1) * 8, menuItems[n], maxLen,
			n == i ? WHITE : GRAY2, BLUE);
		glyph(x, y + (n + 1) * 8, n == i ? '>' : ' ', WHITE, BLUE);
		glyph(x + width - 8, y + (n + 1) * 8,
			n == i ? '<' : ' ', WHITE, BLUE);
	}
	p = pScroller;
	for(n = 0; n < maxLen + 2; ++n)
	{
		if(!*p)
			p = scroller;
		buf[n] = *p ? *p++ : ' ';
	}
	buf[n] = 0;
	text(x, y + (height + 1) * 8, buf, maxLen + 2, CYAN, BLUE);
	if(*pScroller)
		++pScroller;
	if(!*pScroller)
		pScroller = scroller;
	present_game(x - 8, y - 8, width + 16, (height + 4) * 8);
	painted = i;
	last = frames();
	for(;;)
	{
		if(painted != i)
		{
			for(n = 1; n < count; ++n)
			{
				if(n != painted && n != i)
					continue;
				text(x + 8, y + (n + 1) * 8, menuItems[n], maxLen,
					n == i ? WHITE : GRAY2, BLUE);
				glyph(x, y + (n + 1) * 8, n == i ? '>' : ' ', WHITE, BLUE);
				glyph(x + width - 8, y + (n + 1) * 8,
					n == i ? '<' : ' ', WHITE, BLUE);
				present_game(x, y + (n + 1) * 8, width, 8);
			}
			painted = i;
		}
		if(frames() - last >= 8)
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
			present_game(x, y + (height + 1) * 8, width, 8);
			last = frames();
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
			i = (char)(i == 1 ? count - 1 : i - 1);
		else if(keys & INPUT_DOWN)
			i = (char)(i == count - 1 ? 1 : i + 1);
		if(keys & (INPUT_SELECT | INPUT_BACKUP))
			break;
		WaitTOF();
	}
	sc_inMenu = 0;
	sc_mouseTile = 255;
	return keys & INPUT_BACKUP ? 0 : i;
}

void plat_DrawBoard(char clearLog)
{
	char i;
	char tile[2], pieces[2], colors[2], outcome;

	sc_defer = 1;
	sc_inMenu = 0;
	fill(0, 0, 264, 8, GREEN);
	fill(0, 0, 8, 200, GREEN);
	fill(264, 0, 56, 200, GREEN);
	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);
	for(i = 0; i < 8; ++i)
	{
		glyph(16 + i * 32, 0, (char)('A' + i), BLACK, GREEN);
		glyph(0, 16 + i * 24, (char)('8' - i), BLACK, GREEN);
	}
	if(!clearLog)
	{
		/* repainting is not a log operation.  the ring walk rewrites
		 * the log globals, so put back whatever the caller had */
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
	sc_defer = 0;
	present_all();
}

void plat_DrawSquare(char position)
{
	char y, x, light, p, white, kind;
	int px, py;
	char paper;

	y = (char)(position / 8);
	x = (char)(position & 7);
	light = (char)!((x & 1) ^ (y & 1));
	p = gChessBoard[y][x];
	white = (char)((p & PIECE_WHITE) != 0);
	kind = (char)(p & PIECE_DATA);
	px = 8 + x * 32;
	py = 8 + y * 24;
	paper = light ? WHITE : BLACK;
	fill(px, py, 32, 24, paper);
	if(kind)
		piece(px, py, kind, (char)(white == light ? 0 : 1), paper,
			light ? BLACK : WHITE);
	if(gShowAttackBoard)
	{
		glyph(px, py + 16,
			digit(gpAttackBoard[giAttackBoardOffset[position][0]]),
			white ? RED : GREEN, BLACK);
		glyph(px + 24, py + 16,
			digit(gpAttackBoard[giAttackBoardOffset[position][1]]),
			white ? GREEN : RED, BLACK);
		glyph(px, py, digit((char)(p & 15)), PURPLE, BLACK);
		glyph(px + 24, py, digit(white), white ? WHITE : GRAY1, BLACK);
	}
	present_game(px, py, 32, 24);
}

void plat_ShowSideToGoLabel(char side)
{
	sc_side = side;
	text(272, 0, gszSideLabel[side], 6, side ? WHITE : BLACK, GREEN);
	present_game(272, 0, 48, 8);
}

void plat_Highlight(char position, char color, char cursor)
{
	int x, y, row, col, px;
	char light;
	unsigned char *p, byte, fg, bg;

	(void)cursor;
	sc_defer = 1;
	plat_DrawSquare(position);
	sc_defer = 0;
	x = 8 + (position & 7) * 32;
	y = 8 + (position / 8) * 24;
	light = (char)!(((position & 7) & 1) ^ ((position / 8) & 1));
	/* recolour the two outer character cells.  keep the piece pixels
	 * and only change which colour a set or clear bit displays in */
	fg = light ? color : BLACK;
	bg = light ? BLACK : color;
	for(row = y; row < y + 24; ++row)
		for(col = 0; col < 8; col += 2)
			for(px = x + col; px <= x + col + 24; px += 24)
			{
				p = sc_chunky + (row + TOP) * STRIDE + px / 2;
				byte = *p;
				*p = (unsigned char)(((byte & 15) ? fg : bg) |
					((byte & 240) ? fg << 4 : bg << 4));
			}
	present_game(x, y, 32, 24);
}

void plat_ShowMessage(char *str, char color)
{
	(void)color;
	text(264, 192, str, 7, RED, GREEN);
	present_game(264, 192, 56, 8);
}

void plat_ClearMessage(void)
{
	text(264, 192, "", 7, RED, GREEN);
	present_game(264, 192, 56, 8);
}

void plat_AddToLogWin(void)
{
	char y;

	for(y = 1; y <= 22; ++y)
	{
		if(undo_FindUndoLine((char)(22 - y)))
		{
			frontend_FormatLogString();
			text(272, y * 8, gLogStrBuffer, 6, gColor[0] ? WHITE : BLACK, GREEN);
		}
		else
			text(272, y * 8, "", 6, WHITE, GREEN);
	}
	present_game(264, 8, 56, 176);
}

void plat_AddToLogWinTop(void)
{
	plat_AddToLogWin();
}

char plat_GetSeed(void)
{
	/* horizontal beam position.  it changes every colour clock, and a
	 * wrong address only makes the openings repeat */
	return *(volatile char *)0xDFF007;
}

void plat_Shutdown(void)
{
	if(sc_win)
	{
		CloseWindow(sc_win);
		sc_win = 0;
	}
	if(sc_screen)
	{
		CloseScreen(sc_screen);
		sc_screen = 0;
	}
	if(sc_chip)
	{
		FreeMem(sc_chip, PLANESZ * 4 * 2);
		sc_chip = sc_shown = sc_back = 0;
	}
}
