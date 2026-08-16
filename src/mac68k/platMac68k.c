/*
 *	platMac68k.c
 *	cc65 Chess
 *
 *	Macintosh 68k.  Retro68, 68000, System 7.  Gebhart pieces.
 *	mouse move changes the highlight; a click is Return.
 */

#include <string.h>

#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <ToolUtils.h>
#include <Windows.h>

#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "dataMac68k.h"

/*-----------------------------------------------------------------------*/
#define SQUARE				32
#define BOARD_LEFT			22
#define BOARD_TOP			16
#define WIN_W				470
#define WIN_H				296
#define LOG_X				292
#define LOG_TOP				16
#define LOG_LINE			14
#define LOG_LINES			16
#define MSG_Y				280
#define SCROLL_TICKS		8

#define COL_BLACK			0
#define COL_WHITE			1
#define COL_RED				2
#define COL_MAGENTA			3
#define COL_GREEN			4
#define COL_CYAN			5
#define COL_YELLOW			6
#define COL_BLUE			7
#define FONT_MONACO			4

static const RGBColor sc_rgb[8] =
{
	{0x0000, 0x0000, 0x0000},
	{0xFFFF, 0xFFFF, 0xFFFF},
	{0xDDDD, 0x1111, 0x1111},
	{0xCCCC, 0x0000, 0xCCCC},
	{0x1111, 0xBBBB, 0x1111},
	{0x0000, 0xCCCC, 0xCCCC},
	{0xEEEE, 0xCCCC, 0x0000},
	{0x2222, 0x4444, 0xDDDD}
};

static const short sc_qdcolor[8] =
{
	blackColor, whiteColor, redColor, magentaColor,
	greenColor, cyanColor, yellowColor, blueColor
};

static const RGBColor sc_cream = {0xE8E8, 0xD4D4, 0xA0A0};
static const RGBColor sc_olive = {0x5080, 0x7040, 0x3040};
static const RGBColor sc_felt  = {0x1C1C, 0x5555, 0x3333};
static const RGBColor sc_panel = {0x1010, 0x3030, 0x2020};

static WindowPtr sc_win;
static char sc_color;
static char sc_side;
static char sc_msg[32];
static char sc_msgColor;
static char sc_inMenu;
static char sc_ready;
static char sc_splash;
static char sc_boardCursor;
static char sc_haveMouse;
static char sc_pendingSelect;
static Point sc_lastMouse;
static short sc_menuL, sc_menuR, sc_menuT;
static char sc_menuN;

/*-----------------------------------------------------------------------*/
static void square_rect(char position, Rect *r)
{
	char y = position / 8;
	char x = position & 7;

	r->left = BOARD_LEFT + x * SQUARE;
	r->top = BOARD_TOP + y * SQUARE;
	r->right = r->left + SQUARE;
	r->bottom = r->top + SQUARE;
}

/*-----------------------------------------------------------------------*/
static char hit_square(Point pt)
{
	short x, y;

	if(pt.h < BOARD_LEFT || pt.v < BOARD_TOP)
		return 255;
	x = (short)((pt.h - BOARD_LEFT) / SQUARE);
	y = (short)((pt.v - BOARD_TOP) / SQUARE);
	if(x > 7 || y > 7)
		return 255;
	return (char)(y * 8 + x);
}

/*-----------------------------------------------------------------------*/
static char hit_menu_item(Point pt)
{
	short rel;

	if(!sc_inMenu || sc_menuN < 2)
		return 0;
	if(pt.h < sc_menuL + 6 || pt.h >= sc_menuR - 6)
		return 0;
	rel = (short)(pt.v - (sc_menuT + 8));
	if(rel < 16)
		return 0;
	rel = (short)(rel / 16);
	if(rel < 1 || rel >= sc_menuN)
		return 0;
	return (char)rel;
}

/*-----------------------------------------------------------------------*/
/* 1 if the board cursor should jump.  only when the mouse itself moved. */
static char apply_board_hover(void)
{
	Point pt;
	char tile;

	if(sc_inMenu || !sc_ready || sc_splash)
		return 0;

	if(sc_win)
		SetPort(sc_win);
	GetMouse(&pt);
	if(!sc_haveMouse)
	{
		sc_haveMouse = 1;
		sc_lastMouse = pt;
		return 0;
	}
	if(pt.h == sc_lastMouse.h && pt.v == sc_lastMouse.v)
		return 0;
	sc_lastMouse = pt;

	tile = hit_square(pt);
	if(tile == 255 || tile == sc_boardCursor)
		return 0;
	gGotoTile = tile;
	return 1;
}

/*-----------------------------------------------------------------------*/
static void set_fore(char color)
{
	color &= 7;
	if(sc_color)
		RGBForeColor(&sc_rgb[color]);
	else
		ForeColor(sc_qdcolor[color]);
}

/*-----------------------------------------------------------------------*/
static void fill_rgb(const Rect *r, const RGBColor *c, Pattern *bw)
{
	if(sc_color)
	{
		RGBForeColor(c);
		PaintRect(r);
	}
	else
		FillRect(r, bw);
}

/*-----------------------------------------------------------------------*/
static void fill_index(const Rect *r, char color)
{
	set_fore(color);
	PaintRect(r);
}

/*-----------------------------------------------------------------------*/
static void draw_text(short x, short y, const char *s, char color)
{
	set_fore(color);
	MoveTo(x, y);
	DrawText(s, 0, (short)strlen(s));
}

/*-----------------------------------------------------------------------*/
static void draw_ntext(short x, short y, const char *s, char n, char color)
{
	set_fore(color);
	MoveTo(x, y);
	DrawText(s, 0, n);
}

/*-----------------------------------------------------------------------*/
static char hexdigit(char n)
{
	n &= 0x0F;
	return (char)(n < 10 ? '0' + n : 'A' + (n - 10));
}

/*-----------------------------------------------------------------------*/
static void set_port(void)
{
	if(sc_win)
		SetPort(sc_win);
}

/*-----------------------------------------------------------------------*/
static void paint_chrome(void)
{
	Rect r;
	char i;
	char label[2];

	set_port();
	SetRect(&r, 0, 0, WIN_W, WIN_H);
	fill_rgb(&r, &sc_felt, &qd.black);
	SetRect(&r, LOG_X - 8, 0, WIN_W, WIN_H);
	fill_rgb(&r, &sc_panel, &qd.black);

	TextFont(FONT_MONACO);
	TextSize(9);
	label[1] = 0;
	for(i = 0; i < 8; ++i)
	{
		label[0] = (char)('A' + i);
		draw_text((short)(BOARD_LEFT + i * SQUARE + 12), BOARD_TOP - 4,
			label, COL_YELLOW);
		label[0] = (char)('8' - i);
		draw_text(6, (short)(BOARD_TOP + i * SQUARE + 20),
			label, COL_YELLOW);
	}
}

/*-----------------------------------------------------------------------*/
static void paint_log(void)
{
	char saveTile[2], savePiece[2], saveColor[2], saveOutcome;
	char i;
	Rect r;
	char col;

	saveTile[0] = gTile[0];
	saveTile[1] = gTile[1];
	savePiece[0] = gPiece[0];
	savePiece[1] = gPiece[1];
	saveColor[0] = gColor[0];
	saveColor[1] = gColor[1];
	saveOutcome = gOutcome;

	set_port();
	TextFont(FONT_MONACO);
	TextSize(10);
	SetRect(&r, LOG_X, LOG_TOP, WIN_W - 8, LOG_TOP + LOG_LINES * LOG_LINE);
	fill_rgb(&r, &sc_panel, &qd.black);

	for(i = 0; i < LOG_LINES; ++i)
	{
		if(undo_FindUndoLine((char)(LOG_LINES - 1 - i)))
		{
			frontend_FormatLogString();
			col = gColor[0] ? COL_WHITE : COL_YELLOW;
			draw_ntext(LOG_X, (short)(LOG_TOP + (i + 1) * LOG_LINE - 3),
				gLogStrBuffer, 6, col);
		}
	}

	gTile[0] = saveTile[0];
	gTile[1] = saveTile[1];
	gPiece[0] = savePiece[0];
	gPiece[1] = savePiece[1];
	gColor[0] = saveColor[0];
	gColor[1] = saveColor[1];
	gOutcome = saveOutcome;
}

/*-----------------------------------------------------------------------*/
static void paint_message(void)
{
	Rect r;

	set_port();
	SetRect(&r, LOG_X, MSG_Y - 12, WIN_W - 8, WIN_H);
	fill_rgb(&r, &sc_panel, &qd.black);
	if(sc_msg[0])
	{
		TextFont(FONT_MONACO);
		TextSize(10);
		draw_text(LOG_X, MSG_Y, sc_msg, sc_msgColor);
	}
}

/*-----------------------------------------------------------------------*/
static void paint_all(void)
{
	char i;

	paint_chrome();
	for(i = 0; i < 64; ++i)
		plat_DrawSquare(i);
	plat_ShowSideToGoLabel(sc_side);
	paint_log();
	paint_message();
}

/*-----------------------------------------------------------------------*/
static void blit_bits(short dx, short dy, const unsigned char *bits, char color)
{
	BitMap bm;
	Rect src, dst;

	SetRect(&src, 0, 0, SQUARE, SQUARE);
	SetRect(&dst, dx, dy, (short)(dx + SQUARE), (short)(dy + SQUARE));
	bm.baseAddr = (Ptr)bits;
	bm.rowBytes = 4;
	bm.bounds = src;
	set_fore(color);
	CopyBits(&bm, &qd.thePort->portBits, &src, &dst, srcOr, 0);
}

/*-----------------------------------------------------------------------*/
static void blit_piece(short dx, short dy, char piece, char white)
{
	/* solid in the piece colour, then the outline in the opposite so
	 * white pieces stay visible on cream */
	blit_bits(dx, dy, gfxTiles[piece - 1][1], white ? COL_WHITE : COL_BLACK);
	blit_bits(dx, dy, gfxTiles[piece - 1][0], white ? COL_BLACK : COL_WHITE);
}

/*-----------------------------------------------------------------------*/
static void paint_splash(void)
{
	short cx;

	set_port();
	paint_chrome();

	cx = (short)(BOARD_LEFT + 3 * SQUARE + 16);
	blit_piece(cx, (short)(BOARD_TOP + 24), KING, 0);
	blit_piece(cx, (short)(BOARD_TOP + 168), KING, 1);

	TextFont(FONT_MONACO);
	TextSize(10);
	draw_text(BOARD_LEFT + 8, BOARD_TOP + 118, gszAbout, COL_YELLOW);
	draw_text(BOARD_LEFT + 8, BOARD_TOP + 136, "Macintosh 68k.  press a key.", COL_WHITE);
}

/*-----------------------------------------------------------------------*/
static int map_key(char ch, unsigned char key, char autoRepeat)
{
	if(ch >= 'A' && ch <= 'Z')
		ch = (char)(ch - 'A' + 'a');

	if(ch == 30 || key == 0x7E)
		return INPUT_UP;
	if(ch == 31 || key == 0x7D)
		return INPUT_DOWN;
	if(ch == 28 || key == 0x7B)
		return INPUT_LEFT;
	if(ch == 29 || key == 0x7C)
		return INPUT_RIGHT;

	if(autoRepeat)
		return 0;

	if(ch == 13 || ch == 3)
		return INPUT_SELECT;
	if(ch == 27)
		return INPUT_BACKUP;
	if(ch == 'a')
		return INPUT_TOGGLE_A;
	if(ch == 'b')
		return INPUT_TOGGLE_B;
	if(ch == 'd')
		return INPUT_TOGGLE_D;
	if(ch == 'm')
		return INPUT_MENU;
	if(ch == 'u')
		return INPUT_UNDO;
	if(ch == 'r')
		return INPUT_REDO;
	return 0;
}

/*-----------------------------------------------------------------------*/
static int handle_mouse(EventRecord *e)
{
	WindowPtr win;
	short part;
	Rect drag;

	part = FindWindow(e->where, &win);
	switch(part)
	{
		case inGoAway:
			if(win == sc_win && TrackGoAway(win, e->where))
			{
				plat_Shutdown();
				ExitToShell();
			}
			return 0;

		case inDrag:
			drag = qd.screenBits.bounds;
			DragWindow(win, e->where, &drag);
			return 0;

		case inContent:
			if(win != sc_win)
				return 0;
			if(win != FrontWindow())
			{
				SelectWindow(win);
				return 0;
			}
			/* click is Return.  if the pointer also moved onto a new
			 * square, jump there first and deliver SELECT next call */
			if(apply_board_hover())
			{
				sc_pendingSelect = 1;
				return INPUT_UP;
			}
			return INPUT_SELECT;

		default:
			return 0;
	}
}

/*-----------------------------------------------------------------------*/
static void handle_update(WindowPtr win)
{
	if(win != sc_win)
		return;
	BeginUpdate(win);
	set_port();
	if(sc_splash)
		paint_splash();
	else if(sc_ready && !sc_inMenu)
		paint_all();
	EndUpdate(win);
}

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
	long response;
	Rect bounds;

	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(0);
	InitCursor();
	MaxApplZone();

	if(Gestalt(gestaltSystemVersion, &response) != noErr || response < 0x0700)
		ExitToShell();

	sc_color = 0;
	if(Gestalt(gestaltQuickdrawVersion, &response) == noErr &&
		response >= 0x0100)
		sc_color = 1;

	gReturnToOS = 1;
	sc_side = SIDE_WHITE;
	sc_msg[0] = 0;
	sc_ready = 0;
	sc_splash = 1;
	sc_boardCursor = 255;
	sc_haveMouse = 0;
	sc_pendingSelect = 0;

	SetRect(&bounds, 20, 44, 20 + WIN_W, 44 + WIN_H);
	if(sc_color)
		sc_win = NewCWindow(0, &bounds, "\pcc65 Chess", 1, noGrowDocProc,
			(WindowPtr)-1L, 1, 0);
	else
		sc_win = NewWindow(0, &bounds, "\pcc65 Chess", 1, noGrowDocProc,
			(WindowPtr)-1L, 1, 0);

	if(!sc_win)
		ExitToShell();

	set_port();
	paint_splash();
	FlushEvents(everyEvent, 0);

	/* any key or a click in the window, same contract as the C64.
	 * updateEvt must redraw the splash, not the empty board */
	for(;;)
	{
		EventRecord e;
		WindowPtr win;
		short part;
		char ch;

		if(!WaitNextEvent(everyEvent, &e, 10, 0))
			continue;

		if(e.what == keyDown)
		{
			ch = (char)(e.message & charCodeMask);
			if((e.modifiers & cmdKey) && (ch == 'q' || ch == 'Q'))
			{
				plat_Shutdown();
				ExitToShell();
			}
			break;
		}
		if(e.what == mouseDown)
		{
			part = FindWindow(e.where, &win);
			if(part == inGoAway && win == sc_win && TrackGoAway(win, e.where))
			{
				plat_Shutdown();
				ExitToShell();
			}
			else if(part == inDrag)
				DragWindow(win, e.where, &qd.screenBits.bounds);
			else if(part == inContent && win == sc_win)
				break;
		}
		if(e.what == updateEvt)
			handle_update((WindowPtr)e.message);
	}
	sc_splash = 0;
}

/*-----------------------------------------------------------------------*/
void plat_UpdateScreen(void)
{
}

/*-----------------------------------------------------------------------*/
char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller;
	static char *pScroller;
	int keyMask;
	char i, n, maxLen, len;
	short boxW, boxH, boxL, boxT;
	Rect box, inner;
	unsigned long lastTick;
	char buf[40];
	char si;
	char *sp;
	unsigned long ignore;

	sc_inMenu = 1;
	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
	}

	maxLen = 0;
	for(n = 0; menuItems[n]; ++n)
	{
		len = (char)strlen(menuItems[n]);
		if(len > maxLen)
			maxLen = len;
	}
	if(maxLen > 36)
		maxLen = 36;

	boxW = (short)(maxLen * 7 + 28);
	boxH = (short)((height + 3) * 16);
	boxL = (short)((WIN_W - boxW) / 2);
	boxT = (short)((WIN_H - boxH) / 2);
	SetRect(&box, boxL, boxT, boxL + boxW, boxT + boxH);
	sc_menuL = boxL;
	sc_menuR = (short)(boxL + boxW);
	sc_menuT = boxT;
	sc_menuN = n;
	sc_haveMouse = 0;

	lastTick = TickCount();
	i = 1;
	do
	{
		set_port();
		fill_index(&box, COL_BLUE);
		PenSize(2, 2);
		set_fore(COL_YELLOW);
		FrameRect(&box);
		PenSize(1, 1);

		TextFont(FONT_MONACO);
		TextSize(10);
		draw_ntext((short)(boxL + 10), (short)(boxT + 16),
			menuItems[0], (char)strlen(menuItems[0]), COL_YELLOW);

		for(n = 1; menuItems[n]; ++n)
		{
			if(n == i)
			{
				SetRect(&inner, boxL + 6, boxT + 8 + n * 16,
					boxL + boxW - 6, boxT + 24 + n * 16);
				fill_index(&inner, COL_YELLOW);
				draw_ntext((short)(boxL + 12), (short)(boxT + 22 + n * 16),
					menuItems[n], (char)strlen(menuItems[n]), COL_BLACK);
			}
			else
				draw_ntext((short)(boxL + 12), (short)(boxT + 22 + n * 16),
					menuItems[n], (char)strlen(menuItems[n]), COL_WHITE);
		}

		sp = pScroller;
		for(si = 0; si < maxLen + 2 && si < 38; ++si)
		{
			if(!*sp)
				sp = scroller;
			buf[si] = *sp ? *sp : ' ';
			if(*sp)
				++sp;
		}
		buf[si] = 0;
		draw_text((short)(boxL + 10), (short)(boxT + boxH - 8), buf, COL_CYAN);

		{
			Point pt;
			char hit;

			set_port();
			GetMouse(&pt);
			if(!sc_haveMouse)
			{
				sc_haveMouse = 1;
				sc_lastMouse = pt;
			}
			else if(pt.h != sc_lastMouse.h || pt.v != sc_lastMouse.v)
			{
				sc_lastMouse = pt;
				hit = hit_menu_item(pt);
				if(hit)
					i = hit;
			}
		}

		keyMask = plat_ReadKeys(0);
		if(keyMask & INPUT_MOTION)
		{
			switch(keyMask & INPUT_MOTION)
			{
				case INPUT_UP:
					if(!--i)
						i = (char)(n - 1);
					break;
				case INPUT_DOWN:
					if(n == ++i)
						i = 1;
					break;
			}
		}
		keyMask &= (INPUT_SELECT | INPUT_BACKUP);

		if(TickCount() - lastTick >= SCROLL_TICKS)
		{
			lastTick = TickCount();
			++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
		if(!keyMask)
			Delay(1, &ignore);
	} while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);

	sc_inMenu = 0;
	sc_pendingSelect = 0;
	if(keyMask & INPUT_BACKUP)
		return 0;
	return i;
}

/*-----------------------------------------------------------------------*/
void plat_DrawBoard(char clearLog)
{
	/* chrome wipes the window; rebuild everything.  clearLog is the
	 * 8-bit ports emptying a log column; paint_log walks the undo
	 * ring, so both paths are the same here */
	(void)clearLog;
	sc_ready = 1;
	paint_all();
}

/*-----------------------------------------------------------------------*/
void plat_DrawSquare(char position)
{
	Rect r;
	char y = position / 8;
	char x = position & 7;
	char light = !((x & 1) ^ (y & 1));
	char piece, colour;
	char ch;

	set_port();
	square_rect(position, &r);
	if(sc_color)
		fill_rgb(&r, light ? &sc_cream : &sc_olive, &qd.white);
	else
		FillRect(&r, light ? &qd.white : &qd.ltGray);

	piece = gChessBoard[y][x];
	colour = piece & PIECE_WHITE;
	piece &= PIECE_DATA;

	if(gShowAttackBoard)
	{
		TextFont(FONT_MONACO);
		TextSize(9);
		ch = hexdigit(gpAttackBoard[giAttackBoardOffset[position][0]]);
		draw_ntext((short)(r.left + 2), (short)(r.bottom - 3), &ch, 1, COL_RED);
		ch = hexdigit(gpAttackBoard[giAttackBoardOffset[position][1]]);
		draw_ntext((short)(r.right - 8), (short)(r.bottom - 3), &ch, 1, COL_BLUE);
		ch = hexdigit((char)(gChessBoard[y][x] & 0x0F));
		draw_ntext((short)(r.left + 2), (short)(r.top + 10), &ch, 1, COL_MAGENTA);
		ch = hexdigit((char)(colour >> 7));
		draw_ntext((short)(r.right - 8), (short)(r.top + 10), &ch, 1, COL_YELLOW);
	}

	if(piece)
		blit_piece(r.left, r.top, piece, colour);
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	Rect r;

	sc_side = side;
	set_port();
	TextFont(FONT_MONACO);
	TextSize(10);
	SetRect(&r, LOG_X, 2, WIN_W - 8, LOG_TOP);
	fill_rgb(&r, &sc_panel, &qd.black);
	draw_text(LOG_X, 12, gszSideLabel[side],
		side ? COL_WHITE : COL_YELLOW);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char cursor)
{
	Rect r;
	short inset;

	set_port();
	if(cursor)
		sc_boardCursor = position;
	square_rect(position, &r);
	inset = cursor ? 1 : 3;
	InsetRect(&r, inset, inset);
	set_fore(color);
	PenSize(cursor ? 3 : 2, cursor ? 3 : 2);
	FrameRect(&r);
	PenSize(1, 1);
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char color)
{
	char n;

	n = 0;
	while(str[n] && n < 31)
	{
		sc_msg[n] = str[n];
		++n;
	}
	sc_msg[n] = 0;
	sc_msgColor = color ? color : COL_YELLOW;
	paint_message();
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage(void)
{
	sc_msg[0] = 0;
	paint_message();
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWin(void)
{
	paint_log();
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWinTop(void)
{
	paint_log();
}

/*-----------------------------------------------------------------------*/
int plat_ReadKeys(char blocking)
{
	EventRecord e;
	int mask;
	char ch;
	unsigned char key;
	char autoRepeat;

	if(sc_pendingSelect)
	{
		sc_pendingSelect = 0;
		return INPUT_SELECT;
	}

	for(;;)
	{
		if(!WaitNextEvent(everyEvent, &e, blocking ? 1 : 0, 0))
		{
			if(apply_board_hover())
				return INPUT_UP;
			if(!blocking)
				return 0;
			continue;
		}

		switch(e.what)
		{
			case keyDown:
			case autoKey:
				ch = (char)(e.message & charCodeMask);
				key = (unsigned char)((e.message & keyCodeMask) >> 8);
				if((e.modifiers & cmdKey) && (ch == 'q' || ch == 'Q'))
				{
					plat_Shutdown();
					ExitToShell();
				}
				if((e.modifiers & cmdKey) && ch == '.')
					return INPUT_BACKUP;
				autoRepeat = (char)(e.what == autoKey);
				mask = map_key(ch, key, autoRepeat);
				if(mask)
					return mask;
				break;

			case mouseDown:
				mask = handle_mouse(&e);
				if(mask)
					return mask;
				break;

			case updateEvt:
				handle_update((WindowPtr)e.message);
				break;

			default:
				if(apply_board_hover())
					return INPUT_UP;
				break;
		}

		if(apply_board_hover())
			return INPUT_UP;
		if(!blocking)
			return 0;
	}
}

/*-----------------------------------------------------------------------*/
void plat_Shutdown(void)
{
	if(sc_win)
	{
		DisposeWindow(sc_win);
		sc_win = 0;
	}
}

/*-----------------------------------------------------------------------*/
char plat_GetSeed(void)
{
	/* TickCount is 60 Hz.  a wrong read repeats openings. */
	return (char)TickCount();
}
