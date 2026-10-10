/* platform contract checks.  native SWI doubles do not replace playing
 * the ARM executable in Arculator; they catch mouse and repaint regressions */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/archie/platArchie.c"

static unsigned char screen[STRIDE * 256 + 32];
static unsigned char held[128];
static unsigned char visibleBefore[STRIDE * 256];
static int checkPresentation;
static int mx, my, buttons, mode = 12, pointer = 1;
static uint32_t clockNow = 100;

_kernel_oserror *_kernel_swi(int swi, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
	if(swi == OS_ReadMonotonicTime)
		out->r[0] = (int)clockNow;
	else if(swi == OS_Mouse)
	{
		out->r[0] = mx * 4;
		out->r[1] = (255 - my - TOP) * 4;
		out->r[2] = buttons;
	}
	else if(swi == OS_Byte && in->r[0] == 135)
		out->r[2] = mode;
	else if(swi == OS_Byte && in->r[0] == 106)
	{
		int old = pointer;
		pointer = in->r[1];
		out->r[1] = old;
	}
	return 0;
}
int _kernel_osbyte(int a, int b, int c)
{
	(void)c;
	if(a == 106) pointer = b;
	return 0;
}
int _kernel_oswrch(int ch) { return ch; }
void v_setMode(unsigned int n) { mode = (int)n; }
void v_disableTextCursor(void) {}
void *v_getScreenAddress(void) { return screen + 16; }
void v_setBorderColourRGB(unsigned char r, unsigned char g, unsigned char b)
{ (void)r; (void)g; (void)b; }
void v_waitForVSync(void)
{
	if(checkPresentation)
	{
		assert(sc_screen == screen + 16);
		assert(!memcmp(sc_screen, visibleBefore, sizeof(visibleBefore)));
		checkPresentation = 0;
	}
	clockNow += 2;
}
int k_checkKeypress(unsigned int key) { return held[key]; }

char undo_FindUndoLine(char back)
{
	if(back) return 0;
	gTile[0] = 52; gTile[1] = 36;
	gPiece[0] = PAWN; gPiece[1] = NONE;
	gColor[0] = SIDE_WHITE; gColor[1] = SIDE_BLACK;
	gOutcome = OUTCOME_OK;
	return 1;
}
void frontend_FormatLogString(void) { strcpy(gLogStrBuffer, "E2-E4"); }

int main(void)
{
	char before[7], after[7];
	unsigned char labels[264 * 8 / 2 + 4 * 200];
	int i, key;
	sc_screen = screen + 16;
	memset(screen, 0xa5, sizeof(screen));
	memset(sc_screen, 0, STRIDE * 256);
	for(i = 0; i < 64; ++i)
	{
		giAttackBoardOffset[i][0] = i * 2;
		giAttackBoardOffset[i][1] = i * 2 + 1;
	}
	gChessBoard[7][4] = KING | PIECE_WHITE;
	gChessBoard[0][4] = KING;
	gShowAttackBoard = 1;
	gTile[0] = 1; gTile[1] = 2;
	gPiece[0] = 3; gPiece[1] = 4;
	gColor[0] = 5; gColor[1] = 6; gOutcome = 7;
	memcpy(before, gTile, 2); memcpy(before + 2, gPiece, 2);
	memcpy(before + 4, gColor, 2); before[6] = gOutcome;
	plat_DrawBoard(0);
	memcpy(after, gTile, 2); memcpy(after + 2, gPiece, 2);
	memcpy(after + 4, gColor, 2); after[6] = gOutcome;
	assert(!memcmp(before, after, 7));
	for(i = 0; i < 8; ++i)
		memcpy(labels + i * 132, sc_screen + (TOP + i) * STRIDE, 132);
	/* undo and redo both use this repaint: restore even damaged labels */
	for(i = 0; i < 200; ++i)
		memcpy(labels + 132 * 8 + i * 4, sc_screen + (TOP + i) * STRIDE, 4);
	fill(0, 0, 264, 8, GREEN);
	fill(0, 0, 8, 200, GREEN);
	memcpy(visibleBefore, sc_screen, sizeof(visibleBefore));
	checkPresentation = 1;
	plat_DrawBoard(0);
	assert(!checkPresentation);
	for(i = 0; i < 8; ++i)
		assert(!memcmp(labels + i * 132, sc_screen + (TOP + i) * STRIDE, 132));
	for(i = 0; i < 200; ++i)
		assert(!memcmp(labels + 132 * 8 + i * 4,
			sc_screen + (TOP + i) * STRIDE, 4));
	for(i = 0; i < 64; ++i) plat_Highlight((char)i, HCOLOR_VALID, 1);
	for(i = 0; i < 16; ++i)
	{
		assert(screen[i] == 0xa5);
		assert(screen[STRIDE * 256 + 16 + i] == 0xa5);
	}

	/* click a different square: jump first, SELECT next read.  a held
 * button cannot select twice, and clicks outside the board do nothing */
	mx = 8 + 4 * 32 + 16; my = 8 + 6 * 24 + 12;
	scan_mouse(1);
	buttons = 4;
	assert(scan_mouse(1) == INPUT_UP && gGotoTile == 52);
	assert(plat_ReadKeys(1) == INPUT_SELECT);
	assert(scan_mouse(1) == 0);
	buttons = 0; scan_mouse(1);
	mx = 300; buttons = 4;
	assert(scan_mouse(1) == 0);
	buttons = 0; scan_mouse(1);
	mx = 150; buttons = 4; gGotoTile = 255;
	assert(scan_mouse(0) == 0 && gGotoTile == 255);
	buttons = 0; scan_mouse(1);

	/* menu clicks select the item actually under the pointer, even if
 * it has not moved since a nested menu opened */
	sc_inMenu = 1; sc_menuX = 80; sc_menuY = 60;
	sc_menuWidth = 160; sc_menuCount = 5;
	mx = 140; my = 60 + 4 * 8; buttons = 4;
	assert(scan_mouse(0) == INPUT_SELECT && sc_menuHover == 3);
	sc_inMenu = 0; buttons = 0; scan_mouse(0);

	/* right click is Escape everywhere, including during AI search */
	sc_pendingSelect = 1; gGotoTile = 52; buttons = 5;
	assert(scan_mouse(0) == INPUT_BACKUP);
	assert(!sc_pendingSelect && gGotoTile == 255);
	assert(scan_mouse(0) == 0);
	buttons = 0; scan_mouse(0);
	sc_inMenu = 1; buttons = 1;
	assert(scan_mouse(1) == INPUT_BACKUP);
	sc_inMenu = 0; buttons = 0; scan_mouse(0);

	/* toggles and Return have rising edges; only arrows repeat */
	held[KEY_B] = 1;
	assert(scan_keyboard(100) == INPUT_TOGGLE_B);
	assert(scan_keyboard(200) == 0);
	held[KEY_B] = 0; scan_keyboard(201);
	held[KEY_UP] = 1;
	assert(scan_keyboard(202) == INPUT_UP);
	assert(scan_keyboard(236) == 0);
	assert(scan_keyboard(237) == INPUT_UP);
	held[KEY_M] = 1;
	key = scan_keyboard(238);
	assert(key & INPUT_MENU);
	assert(scan_keyboard(239) == 0);

	sc_oldMode = 12; sc_oldPointer = 1;
	plat_Shutdown();
	assert(mode == 12 && pointer == 1);
	plat_Shutdown();
	puts("archie: framebuffer bounds, repaint globals, mouse selection, keyboard edges and cleanup ok");
	return 0;
}
