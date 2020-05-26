/*
 *	plat64.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

/*-----------------------------------------------------------------------*\
 * I really wanted to use the Commander X16 built-in APIs (GRAPH in
 * particular) and for the most part I did.  I did end up using conio
 * for text though, as the menu system is set up for monospace fonts and 
 * I wasn't getting what I wanted from the CONSOLE api, although I 
 * probably could have, had I tried a bit harder.  Conio has curso issues
 * which I think will go away with revisions.  Also, clrscr() in conio
 * doesn't play nice with the GRAPH api.  The FRAMEBUFFER api was a 
 * better fit for drawing the pieces, so I used that.  I tried to do as 
 * much as possible in "C" but this does mean I rely on the generated "C"
 * code ending with a result in .a, for example, in places which may 
 * change.  All in all, I am still quite pleased with how easy it is to 
 * mix assembly code and "C" code in cc65.
\*-----------------------------------------------------------------------*/

#include <cx16.h>
#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "dataCX16.h"

/*-----------------------------------------------------------------------*/
// System locations
struct __cx16regs
{
	union
	{
		struct 
		{
			char r0l;
			char r0h;
			char r1l;
			char r1h;
			char r2l;
			char r2h;
			char r3l;
			char r3h;
			char r4l;
			char r4h;
			char r5l;
			char r5h;
			char r6l;
			char r6h;
			char r7l;
			char r7h;
			char r8l;
			char r8h;
			char r9l;
			char r9h;
			char r10l;
			char r10h;
			char r11l;
			char r11h;
			char r12l;
			char r12h;
			char r13l;
			char r13h;
			char r14l;
			char r14h;
			char r15l;
			char r15h;
		} ;
		unsigned int r[16];
	};
};

#define REG16					(*(struct __cx16regs*)0x02)
#define jiffyTime 				((char*)0xA039)
#define CURS_FLAG				((char*)0x037B)

/*-----------------------------------------------------------------------*/
#define BOARD_PIECE_WIDTH		4
#define BOARD_PIECE_HEIGHT		3
#define SCREEN_WIDTH			40
#define SCREEN_HEIGHT			25
#define SCROLL_SPEED			0x0A

/*-----------------------------------------------------------------------*/
// Internal function Prototype
char plat_TimeExpired(unsigned int aTime, char *timerInit);

/*-----------------------------------------------------------------------*/
// Can't use clrscr as it clears both layers
void clearTextLayer(int x, int y, int w, int h)
{
	int sx, sw;
	unsigned int offset = y * 256;	// lines have a stride of 256
	
	// Double width for characters - character then color
	x *= 2;
	offset += x;
	sx = x;
	w = w * 2;
	sw = x + w;
	h += y;

	for(; y < h; y++)
	{
		x = sx;
		for(; x < sw; x+=2)
		{
			vpoke(32, offset++);
			vpoke(0, offset++);
		}
		// Skip to the start of the next area to fill
		offset += 256 - w;
	}
}

/*-----------------------------------------------------------------------*/
// Get the colors into .a .x & .y for the GRAPH function
void plat_setColors(char stroke, char fill, char)
{
    __asm__("sta tmp1");
	__asm__("ldy #%o", fill);
	__asm__("lda (sp), y");
    __asm__("tax");
	__asm__("ldy #%o", stroke);
	__asm__("lda (sp), y");
    __asm__("ldy tmp1");
    __asm__("jsr GRAPH_SET_COLORS");
}

/*-----------------------------------------------------------------------*/
// Draw a rectangle using the GRAPH api
void plat_gfxFill(char stroke, char back, char x, char y, char w, char h)
{
	plat_setColors(stroke, stroke, back);

	REG16.r[0] = x * 8;
	REG16.r[1] = y * 8;
	REG16.r[2] = w * 8;
	REG16.r[3] = h * 8;
	REG16.r[4] = 0;

	__asm__("sec");
	__asm__("jsr GRAPH_DRAW_RECT");
};

/*-----------------------------------------------------------------------*/
// Use conio for text - the GRAPH text is non-proportional and the CONSOLE
// font is also taler than 8.  This is just easier
void plat_showStrXY(char fore, char back, char x, char y, char* str)
{
	textcolor(fore);
	bgcolor(back);
	gotoxy(x,y);
	cputs(str);
	// Something messes up the cursor but this seems to work to 
	// keep it hidden
	*CURS_FLAG = 1;
}

/*-----------------------------------------------------------------------*/
// Use the frameBuffer API to draw the pieces.  The GRAPH api
// uses bytes/pixel and this is bits/pixel
void plat_showPiece(char color, char x, char y, const char *src)
{
	static unsigned char xColor;
	char row, col, i, c;

	// put the color somewhere safe
	__asm__("ldy #%o", color);
	__asm__("lda (sp), y");
	__asm__("sta %v", xColor);

	// do the 24 rows
	for(row = i = 0; row < 24; row++)
	{
		REG16.r[0] = x * 8;
		REG16.r[1] = row + y * 8;
		__asm__("jsr FB_CURSOR_POSITION");

		// and a width of 4 bytes (32 bits / piece)
		for(col = 0; col < 4; i++, col++)
		{
			c = src[i];
			__asm__("ldx %v", xColor);
			__asm__("jsr FB_SET_8_PIXELS");
		}
	}
}

/*-----------------------------------------------------------------------*/
// Local storage
static char sc_vm;	// the existing video mode for quitting
char textStr[41];	// All visible text goes through here

/*-----------------------------------------------------------------------*/
// Called one-time to set up the platform (or computer or whatever)
void plat_Init()
{
	// Setting this to 0 will not show the "Quit" option in the main menu
	gReturnToOS = 1;

	// Map bank 0 in
	VIA1.pra = 0;

	// Video mode and graphics init
	sc_vm = videomode(VIDEOMODE_320x200) ;
	clearTextLayer(0, 0, 40, 25);
	plat_setColors(COLOR_WHITE, COLOR_WHITE, COLOR_GREEN);
	__asm__("jsr GRAPH_CLEAR");

	// Force full-screen.  Scale is a bit whacky but I really don't like the balck bar at the bottom
	VERA.control = 2;
    VERA.display.hstart = 0;
    VERA.display.hstop = 640 / 4;
    VERA.display.vstart = 0;
    VERA.display.vstop = 480/2;
	VERA.control = 0;
	VERA.display.vscale = 53;

	// Show the welcome text
	strcpy(textStr, "Commander X16 version, May 2020.");
    plat_showStrXY(COLOR_YELLOW, COLOR_GREEN, 2,SCREEN_HEIGHT/2-1, gszAbout);
    plat_showStrXY(COLOR_YELLOW, COLOR_GREEN, 4,SCREEN_HEIGHT/2+1,textStr);

	// Show the welcome kings (black and white, solid versions)
	plat_showPiece(COLOR_BLACK, SCREEN_WIDTH/2 - 1, SCREEN_HEIGHT/2 - 6, gfxTiles[KING-1][1]);
	plat_showPiece(COLOR_WHITE, SCREEN_WIDTH/2 - 1, SCREEN_HEIGHT/2 + 4, gfxTiles[KING-1][1]);

    plat_ReadKeys(1);	
}

/*-----------------------------------------------------------------------*/
// This is not needed on the CX16
void plat_UpdateScreen()
{
}

/*-----------------------------------------------------------------------*/
// Very simple menu with a heading and a scrolling banner as a footer
char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller, *pEnd;
	int keyMask;
	char i, sx, sy, numMenuItems, timerInit = 0, maxLen = 0;

	// If the scroller message chages, cache the new one
	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
		pEnd = scroller + strlen(scroller);
	}

	// Find the longest entry
	for(numMenuItems=0; menuItems[numMenuItems]; ++numMenuItems)
	{
		char len = strlen(menuItems[numMenuItems]);
		if(len > maxLen)
			maxLen = len;
	}
	
	// Centre on the screen
	sy = MAX_SIZE(0, (SCREEN_HEIGHT / 2) - (height / 2) - 1);
	sx = MAX_SIZE(0, (SCREEN_WIDTH / 2) - (maxLen / 2) - 1);
	maxLen = MIN_SIZE(SCREEN_WIDTH-2, maxLen);

	// Draw a frame for the menu
	sprintf(textStr, "%-*s", maxLen+4," ");
	plat_showStrXY(COLOR_YELLOW, COLOR_YELLOW, sx-1, sy-1, textStr);
	plat_showStrXY(COLOR_YELLOW, COLOR_YELLOW, sx-1, sy+height+2, textStr);
	*(textStr + 1) = '\0';
	for(i = 0; i < height + 2; i++)
	{
		plat_showStrXY(COLOR_YELLOW, COLOR_YELLOW, sx-1, sy+i, textStr);
		plat_showStrXY(COLOR_YELLOW, COLOR_YELLOW, sx+maxLen+2, sy+i, textStr);
	}

	// Show the title
	sprintf(textStr, " %.*s ",38, menuItems[0]);
	plat_showStrXY(COLOR_YELLOW, COLOR_BLUE, sx, sy, textStr);

	// Leave a blank line
	sprintf(textStr, "%-*s", maxLen+2," ");
	plat_showStrXY(COLOR_WHITE, COLOR_BLUE, sx, ++sy, textStr);
	
	// Show all the menu items
	for(i=1; i<numMenuItems; ++i)
	{
		sprintf(textStr, " %.*s ",maxLen, menuItems[i]);
		plat_showStrXY(COLOR_GRAY2, COLOR_BLUE, sx, sy+i, textStr);
	}
	
	// Pad with blank lines to menu height
	for(;i<height;++i)
	{
		sprintf(textStr, "%-*s", maxLen+2," ");
		plat_showStrXY(COLOR_WHITE, COLOR_BLUE, sx, sy+i, textStr);
	}
	
	// Select the first item
	i = 1;
	do
	{
		// Highlight the selected item
		sprintf(textStr, ">%.*s<",maxLen, menuItems[i]);
		plat_showStrXY(COLOR_WHITE, COLOR_BLUE, sx, sy+i, textStr);
		
		// Look for user input
		keyMask = plat_ReadKeys(0);
		
		if(keyMask & INPUT_MOTION)
		{
			// selection changes so de-highlight the selected item
			sprintf(textStr, " %.*s ",maxLen, menuItems[i]);
			plat_showStrXY(COLOR_GRAY2, COLOR_BLUE, sx, sy+i, textStr);
			
			// see if the selection goes up or down
			switch(keyMask & INPUT_MOTION)
			{
				case INPUT_UP:
					if(!--i)
						i = numMenuItems-1;
				break;
			
				case INPUT_DOWN:
					if(numMenuItems == ++i)
						i = 1;
				break;
			}
		}
		keyMask &= (INPUT_SELECT | INPUT_BACKUP);

		// Show the scroller
		sprintf(textStr, " %.*s ",maxLen, pScroller);
		plat_showStrXY(COLOR_WHITE, COLOR_GREEN, sx, sy+height, textStr);

		// Wrap the message if needed
		if((pEnd - pScroller) < maxLen-1)
		{
			sprintf(textStr, " %.*s ",maxLen-(pEnd - pScroller)-1, scroller);
			plat_showStrXY(COLOR_WHITE, COLOR_GREEN, sx+(pEnd-pScroller)+1, sy+height, textStr);
		}

		// Only update the scrolling when needed
		if(plat_TimeExpired(SCROLL_SPEED, &timerInit))
		{
			++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
	} while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);

	// if backing out of the menu, return 0
	if(keyMask & INPUT_BACKUP)
		return 0;
	
	// return the selection
	return i;
}

/*-----------------------------------------------------------------------*/
// Draw the chess board and possibly clear the log section
void plat_DrawBoard(char clearLog)
{
	char i, c;

	clearTextLayer(0, 0, 40, 25);

	if(clearLog)
	{
		// Clear the log area pixels and color to green
		plat_gfxFill(COLOR_GREEN,COLOR_GREEN,33,0,7,25);
	}

	plat_setColors(COLOR_WHITE, COLOR_GREEN, COLOR_GREEN);
	// $12: ATTRIBUTES: REVERSE so white text shows up on green
	__asm__("lda #$12");
	__asm__("jsr GRAPH_PUT_CHAR");

	// Add the A..H and 1..8 tile-keys before the board since the font draws taller than 8 pixels
	for(i=0; i<8; ++i)
	{
		// Put this into the graphics layer as it's part of the board
		REG16.r[0] = 20 + (i * 32);
		REG16.r[1] = 6;
		c = 'A'+i;
		__asm__("jsr GRAPH_PUT_CHAR");

		REG16.r[0] = 0;
		REG16.r[1] = 24 + (i * 24);
		c = '0'+(8-i);
		__asm__("jsr GRAPH_PUT_CHAR");
	}
	
	// redraw all tiles
	for(i=0; i<64; ++i)
	{
		plat_DrawSquare(i);
	}
}

/*-----------------------------------------------------------------------*/
// Draw a tile with background and piece on it for positions 0..63
void plat_DrawSquare(char position)
{
	char index;
	char piece, color;
	char y = position / 8, x = position & 7;
	char blackWhite = !((x & 1) ^ (y & 1));

	// Draw the boatrd square
	plat_gfxFill(blackWhite, !blackWhite, 1 + x * BOARD_PIECE_WIDTH, 1 + y * BOARD_PIECE_HEIGHT, BOARD_PIECE_WIDTH, BOARD_PIECE_HEIGHT);
	
	
	// Get the piece data to draw the piece over the tile
	piece = gChessBoard[y][x];
	color = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	
	if(piece)
	{
		index = 1;
		if((color && blackWhite) || (!color && !blackWhite))
			index = 0;
		plat_showPiece(!blackWhite, 1 + x * BOARD_PIECE_WIDTH, y * BOARD_PIECE_HEIGHT + 1, gfxTiles[piece-1][index]);
	}

	// Show the attack numbers
	if(gShowAttackBoard)
	{
		char piece_value = (gChessBoard[y][x] & 0x0f);
		char piece_color = (gChessBoard[y][x] & PIECE_WHITE) >> 7;

		// Attackers (bottom left)
		sprintf(textStr, "%d",(gpAttackBoard[giAttackBoardOffset[position][0]]));
		plat_showStrXY(COLOR_WHITE, COLOR_GREEN, 1+x*BOARD_PIECE_WIDTH,(y+1)*BOARD_PIECE_HEIGHT, textStr);

		// Defenders (bottom right)
		sprintf(textStr, "%d",(gpAttackBoard[giAttackBoardOffset[position][1]]));
		plat_showStrXY(COLOR_WHITE, COLOR_GREEN, 1+x*BOARD_PIECE_WIDTH+3,(y+1)*BOARD_PIECE_HEIGHT, textStr);
		
		// Color (0 is black, 128 is white) and piece value (1=ROOK, 2=KNIGHT, 3=BISHOP, 4=QUEEN, 5=KING, 6=PAWN)
		sprintf(textStr, "%0d",piece_value);
		plat_showStrXY(COLOR_WHITE, COLOR_GREEN, 1+x*BOARD_PIECE_WIDTH,1+y*BOARD_PIECE_HEIGHT, textStr);

		// Color
		sprintf(textStr, "%d",piece_color);
		plat_showStrXY(COLOR_WHITE, COLOR_GREEN, 1+x*BOARD_PIECE_WIDTH+3,1+y*BOARD_PIECE_HEIGHT, textStr);
		
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	// Show Black or White (can't print in black?)
	sprintf(textStr, "%s",gszSideLabel[side]);
	plat_showStrXY(side ? COLOR_WHITE : COLOR_GRAY1, COLOR_GREEN, 2+8*BOARD_PIECE_WIDTH, 0, textStr);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char)
{
	char y = position / 8, x = position & 7;
	
	// Draw the two vertical bars - the "cursor"
	plat_gfxFill(color, color, x * 4 + 1, y * 3 + 1, 1, 3);
	plat_gfxFill(color, color, x * 4 + 4, y * 3 + 1, 1, 3);
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char)
{
	// Always an error message - illegal move or no more undo/redo
	sprintf(textStr, "%.*s",SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH),str);
	plat_showStrXY(COLOR_RED, COLOR_GREEN, 1+(8*BOARD_PIECE_WIDTH), SCREEN_HEIGHT-1, textStr);
	
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage()
{
	// Erase the message from ShowMessage
	sprintf(textStr, "%-*s",SCREEN_WIDTH-1-8*BOARD_PIECE_WIDTH," ");
	plat_showStrXY(COLOR_GREEN, COLOR_GREEN, (8*BOARD_PIECE_WIDTH)+1, (8*BOARD_PIECE_HEIGHT), textStr);
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWin()
{
	char bot = (8*BOARD_PIECE_HEIGHT)-2, y = 1, x = 2+(8*BOARD_PIECE_WIDTH);
	char i = 0;

	// Show a log of the moves that have been played
	for(; y<=bot; ++y)
	{
		if(undo_FindUndoLine(bot-y))
		{
			frontend_FormatLogString();
			sprintf(textStr, "%-6s",gLogStrBuffer);
			plat_showStrXY((gColor[0] ? COLOR_WHITE : COLOR_GRAY1), COLOR_GREEN, x, y, textStr);
			
		}
		else
		{
			sprintf(textStr, "%-*s",SCREEN_WIDTH-x," ");
			plat_showStrXY(COLOR_GREEN, COLOR_GREEN, x, y, textStr);
		}
	}
}

/*-----------------------------------------------------------------------*/
// Important note about this function is that it alters the gTile...
// global data trackers so beware when calling it
void plat_AddToLogWinTop()
{
	// This redraws the whole log window so just call it
	plat_AddToLogWin();
}

/*-----------------------------------------------------------------------*/
// Use timer B to time a duration
char plat_TimeExpired(unsigned int aTime, char *timerInit)
{
	VIA1.pra = 0;
	if(!*timerInit || aTime < *jiffyTime )
	{
		*timerInit = 1;
		*jiffyTime = 0;

		return 1;
	}	
	return 0;
}

/*-----------------------------------------------------------------------*/
int plat_ReadKeys(char blocking)
{
	char key = 0;
	int keyMask = 0;
	
	if(blocking || kbhit())
		key = cgetc();
	else
		return 0;
		
	switch(key)
	{
		case 145:		// Up
			keyMask |= INPUT_UP;
		break;

		case 29:		// Right
			keyMask |= INPUT_RIGHT;
		break;

		case 17:		// Down
			keyMask |= INPUT_DOWN;
		break;

		case 157:		// Left
			keyMask |= INPUT_LEFT;
		break;
		
		case 3:			// Esc
			keyMask |= INPUT_BACKUP;
		break;

		case 65:		// 'a' - Show Attackers
			keyMask |= INPUT_TOGGLE_A;
		break;
		
		case 66:		// 'b' - Board attacks - Show all attacks
			keyMask |= INPUT_TOGGLE_B;
		break;
		
		case 68:		// 'd' - Show Defenders
			keyMask |= INPUT_TOGGLE_D;
		break;
		
		case 77:		// 'm' - Menu
			keyMask |= INPUT_MENU;
		break;

		case 13:		// Enter
			keyMask |= INPUT_SELECT;
		break;
		
		case 82:
			keyMask |= INPUT_REDO;
		break;
		
		case 85:
			keyMask |= INPUT_UNDO;
		break;
		
		// default:		// Debug - show key code
		// {
		//	char s[] = "Key:000";
		//	s[4] = (key/100)+'0';
		//	key -= (s[4] - '0') * 100;
		//	s[5] = (key/10)+'0';
		//	s[6] = (key%10)+'0';
		//	plat_ShowMessage(s,COLOR_RED);
		// }
		break;
	}
	
	return keyMask;
}

/*-----------------------------------------------------------------------*/
// Only gets called if gReturnToOS is true, which it isn't
void plat_Shutdown()
{
	videomode(sc_vm);
	clrscr();
	printf("Sadly, you cannot re-run the game...\n");
}
