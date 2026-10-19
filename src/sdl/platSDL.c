/* native SDL2 display, using the original C64 bitmap pieces.
 * game coordinates stay 320x200; the desktop window scales them together. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "undo.h"
#include "frontend.h"
#include "plat.h"
#include "../c64/dataC64.h"

extern const char sc_font[96][8];

#define TOP 20
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

static const Uint32 sc_rgb[16] = {
	0xff000000, 0xffffffff, 0xff880000, 0xffaaffee,
	0xffcc44cc, 0xff00cc55, 0xff0000aa, 0xffeeee77,
	0xffdd8855, 0xff664400, 0xffff7777, 0xff333333,
	0xff777777, 0xffaaff66, 0xff0088ff, 0xffbbbbbb
};
static SDL_Window *sc_window;
static SDL_Renderer *sc_renderer;
static SDL_Texture *sc_texture;
static Uint32 sc_pixels[320 * 240];
static char sc_dirty, sc_inMenu, sc_pendingSelect;
static char sc_mouseTile = 255, sc_side = SIDE_WHITE;
static int sc_menuX, sc_menuY, sc_menuWidth;
static char sc_menuCount, sc_menuHover;

static void fail(void)
{
	fprintf(stderr, "SDL chess: %s\n", SDL_GetError());
	plat_Shutdown();
	exit(EXIT_FAILURE);
}

static void present_all(void)
{
	if(!sc_dirty)
		return;
	if(SDL_UpdateTexture(sc_texture, NULL, sc_pixels, 320 * sizeof(Uint32)) < 0 ||
		SDL_RenderClear(sc_renderer) < 0 ||
		SDL_RenderCopy(sc_renderer, sc_texture, NULL, NULL) < 0)
		fail();
	SDL_RenderPresent(sc_renderer);
	sc_dirty = 0;
}

static void pixel(int x, int y, unsigned char colour)
{
	y += TOP;
	if(x >= 0 && x < 320 && y >= 0 && y < 240)
	{
		sc_pixels[y * 320 + x] = sc_rgb[colour & 15];
		sc_dirty = 1;
	}
}

static void fill(int x, int y, int w, int h, unsigned char colour)
{
	int row, col;

	for(row = y; row < y + h; ++row)
		for(col = x; col < x + w; ++col)
			pixel(col, row, colour);
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

/* source bytes are in VIC cell order: four columns of eight, three rows */
static void piece(int x, int y, char kind, char variant, char paper, char ink)
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

/* SDL logical sizing also transforms mouse event coordinates, including
 * the letterbox offset.  do not transform the event a second time. */
static int mouse_event(int x, int y, char click, char blocking)
{
	char tile;

	y -= TOP;
	if(sc_inMenu)
	{
		if(x >= sc_menuX && x < sc_menuX + sc_menuWidth &&
			y >= sc_menuY + 16 && y < sc_menuY + (sc_menuCount + 1) * 8)
		{
			sc_menuHover = (char)((y - sc_menuY) / 8 - 1);
			return click ? INPUT_SELECT : 0;
		}
		return 0;
	}
	/* search polls must never move the human cursor or queue a selection. */
	if(!blocking || x < 8 || x >= 264 || y < 8 || y >= 200)
		return 0;
	tile = (char)(((y - 8) / 24) * 8 + (x - 8) / 32);
	if(click || tile != sc_mouseTile)
	{
		sc_mouseTile = tile;
		gGotoTile = tile;
		sc_pendingSelect = click;
		return INPUT_UP;
	}
	return 0;
}

int plat_ReadKeys(char blocking)
{
	SDL_Event event;
	int key;

	present_all();
	if(sc_pendingSelect && blocking && !sc_inMenu)
	{
		sc_pendingSelect = 0;
		return INPUT_SELECT;
	}
	for(;;)
	{
		if(!SDL_PollEvent(&event))
		{
			if(!blocking)
				return 0;
			if(!SDL_WaitEvent(&event))
				fail();
		}
		key = 0;
		switch(event.type)
		{
			case SDL_QUIT:
				plat_Shutdown();
				exit(EXIT_SUCCESS);
			case SDL_WINDOWEVENT:
				sc_dirty = 1;
				present_all();
				break;
			case SDL_MOUSEMOTION:
				key = mouse_event(event.motion.x, event.motion.y, 0, blocking);
				break;
			case SDL_MOUSEBUTTONDOWN:
				if(event.button.button == SDL_BUTTON_LEFT)
					key = mouse_event(event.button.x, event.button.y, 1, blocking);
				else if(event.button.button == SDL_BUTTON_RIGHT)
					key = INPUT_BACKUP;
				break;
			case SDL_KEYDOWN:
				sc_mouseTile = 255;
				switch(event.key.keysym.sym)
				{
					case SDLK_UP: key = INPUT_UP; break;
					case SDLK_RIGHT: key = INPUT_RIGHT; break;
					case SDLK_DOWN: key = INPUT_DOWN; break;
					case SDLK_LEFT: key = INPUT_LEFT; break;
					case SDLK_ESCAPE: key = INPUT_BACKUP; break;
					case SDLK_RETURN:
					case SDLK_KP_ENTER: key = INPUT_SELECT; break;
					case SDLK_a: key = INPUT_TOGGLE_A; break;
					case SDLK_b: key = INPUT_TOGGLE_B; break;
					case SDLK_d: key = INPUT_TOGGLE_D; break;
					case SDLK_m: key = INPUT_MENU; break;
					case SDLK_u: key = INPUT_UNDO; break;
					case SDLK_r: key = INPUT_REDO; break;
				}
				break;
		}
		if(key & (INPUT_MENU | INPUT_BACKUP))
		{
			sc_pendingSelect = 0;
			gGotoTile = 255;
		}
		if(key || sc_menuHover)
			return key;
	}
}

void plat_Init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
		fail();
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	sc_window = SDL_CreateWindow("cc65 Chess - C64 graphics", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, 960, 720,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if(!sc_window)
		fail();
	SDL_SetWindowMinimumSize(sc_window, 640, 480);
	sc_renderer = SDL_CreateRenderer(sc_window, -1, SDL_RENDERER_ACCELERATED);
	if(!sc_renderer)
		sc_renderer = SDL_CreateRenderer(sc_window, -1, SDL_RENDERER_SOFTWARE);
	if(!sc_renderer || SDL_RenderSetLogicalSize(sc_renderer, 320, 240) < 0)
		fail();
	sc_texture = SDL_CreateTexture(sc_renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, 320, 240);
	if(!sc_texture)
		fail();
	gReturnToOS = 1;
	fill(0, -TOP, 320, 240, GREEN);
	text(8, -12, "cc65 CHESS", 10, BLACK, GREEN);
	text(8, 204, "M Menu  U Undo  R Redo  B Board", 31, BLACK, GREEN);
	text(8, 212, "A Attackers D Defenders RETURN Select", 39, BLACK, GREEN);
}

void plat_UpdateScreen(void)
{
	present_all();
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
	present_all();
	painted = i;
	last = SDL_GetTicks();
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
				present_all();
			}
			painted = i;
		}
		if(SDL_GetTicks() - last >= 150)
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
			present_all();
			last = SDL_GetTicks();
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
		SDL_Delay(8);
	}
	sc_inMenu = 0;
	sc_mouseTile = 255;
	return keys & INPUT_BACKUP ? 0 : i;
}

void plat_DrawBoard(char clearLog)
{
	char i;
	char tile[2], pieces[2], colors[2], outcome;

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
}

void plat_ShowSideToGoLabel(char side)
{
	sc_side = side;
	text(272, 0, gszSideLabel[side], 6, side ? WHITE : BLACK, GREEN);
}

void plat_Highlight(char position, char color, char cursor)
{
	int x, y, row, col, px;
	char light;
	Uint32 *p;

	(void)cursor;
	plat_DrawSquare(position);
	x = 8 + (position & 7) * 32;
	y = 8 + (position / 8) * 24;
	light = (char)!(((position & 7) & 1) ^ ((position / 8) & 1));
	/* the C64 cursor recolours the two outer character cells. */
	for(row = y; row < y + 24; ++row)
		for(col = 0; col < 8; ++col)
			for(px = x + col; px <= x + col + 24; px += 24)
			{
				p = &sc_pixels[(row + TOP) * 320 + px];
				*p = *p == sc_rgb[BLACK] ? sc_rgb[light ? BLACK : color] :
					sc_rgb[light ? color : BLACK];
			}
	sc_dirty = 1;
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
		if(undo_FindUndoLine((char)(22 - y)))
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
	return (char)SDL_GetPerformanceCounter();
}

void plat_Shutdown(void)
{
	SDL_DestroyTexture(sc_texture);
	SDL_DestroyRenderer(sc_renderer);
	SDL_DestroyWindow(sc_window);
	sc_texture = NULL;
	sc_renderer = NULL;
	sc_window = NULL;
	SDL_Quit();
}
