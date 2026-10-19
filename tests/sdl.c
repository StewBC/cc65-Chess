/* SDL event integration: real human/menu code, no platform stubs.
 * include the port to inspect pixels without adding a shipping test API. */
#include <assert.h>
#include "board.h"
#include "human.h"
#include "cpu.h"
#include "../src/sdl/platSDL.c"

static void key(SDL_Keycode code)
{
	SDL_Event e;

	memset(&e, 0, sizeof(e));
	e.type = SDL_KEYDOWN;
	e.key.keysym.sym = code;
	assert(SDL_PushEvent(&e) == 1);
}

static void click(int tile)
{
	SDL_Event e;

	memset(&e, 0, sizeof(e));
	e.type = SDL_MOUSEBUTTONDOWN;
	e.button.button = SDL_BUTTON_LEFT;
	/* injected events use the logical coordinates SDL normally supplies */
	e.button.x = 8 + (tile & 7) * 32 + 16;
	e.button.y = TOP + 8 + (tile / 8) * 24 + 12;
	assert(SDL_PushEvent(&e) == 1);
}

static void capture(const char *path)
{
	SDL_Surface *surface;
	int w, h;

	assert(SDL_GetRendererOutputSize(sc_renderer, &w, &h) == 0);
	surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	assert(surface);
	assert(SDL_UpdateTexture(sc_texture, NULL, sc_pixels, 320 * sizeof(Uint32)) == 0);
	assert(SDL_RenderClear(sc_renderer) == 0);
	assert(SDL_RenderCopy(sc_renderer, sc_texture, NULL, NULL) == 0);
	assert(SDL_RenderReadPixels(sc_renderer, NULL, surface->format->format,
		surface->pixels, surface->pitch) == 0);
	assert(SDL_SaveBMP(surface, path) == 0);
	SDL_FreeSurface(surface);
}

int main(void)
{
	char before[8], side, level;
	float x, y;

	plat_Init();
	board_Init();
	undo_Init();
	plat_DrawBoard(1);
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	key(SDLK_DOWN);
	key(SDLK_RETURN);
	assert(frontend_Menu(0) == OUTCOME_OK);
	assert(gUserMode == (USER_WHITE | USER_BLACK));

	click(52); /* e2 */
	click(36); /* e4 */
	assert(human_Play(SIDE_WHITE) == OUTCOME_OK);
	assert(gChessBoard[4][4] == (PAWN | PIECE_WHITE));
	assert(gChessBoard[6][4] == NONE);
	assert(undo_FindUndoLine(0));
	assert(gTile[0] == 52 && gTile[1] == 36);
	key(SDLK_u);
	assert(human_Play(SIDE_BLACK) == OUTCOME_OK);
	assert(gChessBoard[6][4] == (PAWN | PIECE_WHITE));
	key(SDLK_r);
	assert(human_Play(SIDE_WHITE) == OUTCOME_OK);
	assert(gChessBoard[4][4] == (PAWN | PIECE_WHITE));
	assert(gTile[0] == 52 && gTile[1] == 36);

	/* keyboard selection still works after using the mouse */
	gCursorPos[SIDE_BLACK][0] = 1;
	gCursorPos[SIDE_BLACK][1] = 4;
	key(SDLK_RETURN);
	key(SDLK_DOWN);
	key(SDLK_DOWN);
	key(SDLK_RETURN);
	assert(human_Play(SIDE_BLACK) == OUTCOME_OK);
	assert(gChessBoard[3][4] == PAWN);
	assert(gChessBoard[1][4] == NONE);

	/* a repaint must preserve all the globals the log walk rewrites */
	memcpy(before, gTile, 2);
	memcpy(before + 2, gPiece, 2);
	memcpy(before + 4, gColor, 2);
	before[6] = gOutcome;
	plat_DrawBoard(0);
	assert(!memcmp(before, gTile, 2));
	assert(!memcmp(before + 2, gPiece, 2));
	assert(!memcmp(before + 4, gColor, 2));
	assert(before[6] == gOutcome);
	plat_ShowSideToGoLabel(SIDE_WHITE);
	capture("scratch/sdl-board.bmp");

	key(SDLK_b);
	key(SDLK_a);
	key(SDLK_d);
	key(SDLK_m);
	assert(human_Play(SIDE_BLACK) == OUTCOME_MENU);
	assert(gShowAttackBoard == 1);
	assert((gShowAttacks[SIDE_BLACK] & 3) == 3);
	capture("scratch/sdl-attacks.bmp");
	key(SDLK_ESCAPE);
	assert(plat_Menu(gSkillMenu, 6, gszAbout) == 0);
	capture("scratch/sdl-menu.bmp");

	/* resizing must keep the centre mapped to the centre of the game */
	SDL_SetWindowSize(sc_window, 1100, 800);
	SDL_PumpEvents();
	SDL_RenderWindowToLogical(sc_renderer, 550, 400, &x, &y);
	assert(x > 159 && x < 161 && y > 119 && y < 121);
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	gGotoTile = 255;
	click(12);
	assert(plat_ReadKeys(0) == 0);
	assert(gGotoTile == 255 && !sc_pendingSelect);

	/* every skill gets real CPU turns against a fresh board */
	for(level = 0; level < 4; ++level)
	{
		board_Init();
		undo_Init();
		gShowAttackBoard = 0;
		gSkillLevel = level;
		for(side = 0; side < 2; ++side)
			assert(cpu_Play((char)(1 - side)) == OUTCOME_OK);
		assert(undo_CanUndo());
	}
	plat_Shutdown();
	puts("SDL: menu, mouse move, undo/redo, B/A/D, resize, all skills passed");
	return 0;
}
