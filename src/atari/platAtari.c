/*
 *	platAtari.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, July 2020.
 *
 */

#include <atari.h>
#include <stdio.h>
#include <string.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "dataAtari.h"


/*-----------------------------------------------------------------------*/
#define SCROLL_SPEED 			10
#define BOARD_PIECE_WIDTH		3
#define BOARD_PIECE_HEIGHT		22
#define SCREEN_WIDTH			40
#define SCREEN_HEIGHT			24
#define LOG_WINDOW_HEIGHT		(SCREEN_HEIGHT - 2)

/*-----------------------------------------------------------------------*/
// Internal function Prototype
char plat_TimeExpired(unsigned char aTime);

/*-----------------------------------------------------------------------*/
// Function Prototype for functions in hiresAtari.s
extern void plat_atariInit(void);
extern void plat_gfxFill(char fill, char x, char y, char w, char h);
extern void plat_showPiece(char x, char Y, const char *src);
extern void plat_showStrXY(char x, char y, char *str);

/*-----------------------------------------------------------------------*/
// Local storage
char textStr[41];
static char subMenu;

/*-----------------------------------------------------------------------*/
void plat_Init()
{
	// Setting this to 0 will not show the "Quit" option in the main menu
	gReturnToOS = 1;

	plat_gfxFill(0,0,0,40,191);
	plat_atariInit();

	_setcolor(1,0xc,0xf);	// Pixel %1 color
	_setcolor(2,0xc,0x7);	// Pixel %0 color
	_setcolor(4,0xC,0x7); 	// border color

	// Show the welcome text
	strcpy(textStr, "Atari graphics version, July 2020.");
    plat_showStrXY(2,SCREEN_HEIGHT/2-1, gszAbout);
    plat_showStrXY(3,SCREEN_HEIGHT/2+1,textStr);

	// Show the welcome kings (black and white, solid versions)
	plat_showPiece(SCREEN_WIDTH/2 - 1, 8*(SCREEN_HEIGHT/2 - 6), gfxTiles[KING-1][1]);
	plat_showPiece(SCREEN_WIDTH/2 - 1, 8*(SCREEN_HEIGHT/2 + 4), gfxTiles[KING-1][0]);

	// Hold the welcome screen
    plat_ReadKeys(1);	

	// Set game colors
	_setcolor(2,0x0,0x0);	// Pixel %0 color
	plat_gfxFill(0,0,0,40,191);

	// Not in a menu
	subMenu = 0;
}

/*-----------------------------------------------------------------------*/
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

	subMenu++;

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

	// Draw and color a frame
	plat_gfxFill(0xAA , sx-1       , 8*(sy-1       ), maxLen+4 , 8*(1       ));
	plat_gfxFill(0xAA , sx-1       , 8*(sy         ), 1        , 8*(height+3));
	plat_gfxFill(0xAA , sx+maxLen+2, 8*(sy         ), 1        , 8*(height+3));
	plat_gfxFill(0xAA , sx         , 8*(sy+height+2), maxLen+2 , 8*(1       ));

	// Show the title
	sprintf(textStr, " %.*s ",38, menuItems[0]);
	plat_showStrXY(sx, sy, textStr);

	// Leave a blank line
	sprintf(textStr, "%-*s", maxLen+2," ");
	plat_showStrXY(sx, ++sy, textStr);
	
	// Show all the menu items
	for(i=1; i<numMenuItems; ++i)
	{
		sprintf(textStr, " %.*s ",maxLen, menuItems[i]);
		plat_showStrXY(sx, sy+i, textStr);
	}
	
	// Pad with blank lines to menu height
	for(;i<height;++i)
	{
		sprintf(textStr, "%-*s", maxLen+2," ");
		plat_showStrXY(sx, sy+i, textStr);
	}
	
	// Select the first item
	i = 1;
	do
	{
		// Highlight the selected item
		sprintf(textStr, ">%.*s<",maxLen, menuItems[i]);
		plat_showStrXY(sx, sy+i, textStr);
		
		// Look for user input
		keyMask = plat_ReadKeys(0);
		
		if(keyMask & INPUT_MOTION)
		{
			// selection changes so de-highlight the selected item
			sprintf(textStr, " %.*s ",maxLen, menuItems[i]);
			plat_showStrXY(sx, sy+i, textStr);

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
		plat_showStrXY(sx, sy+height, textStr);

		// Wrap the message if needed
		if((pEnd - pScroller) < maxLen-1)
		{
			sprintf(textStr, " %.*s ",maxLen-(pEnd - pScroller)-1, scroller);
			plat_showStrXY(sx+(pEnd-pScroller)+1, sy+height, textStr);
		}

		// Only update the scrolling when needed
		if(plat_TimeExpired(SCROLL_SPEED))
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
void plat_DrawBoard(char clearLog)
{
	char i;

	// Not in a menu when drawBoard is called
	if(subMenu)
	{
		clearLog = 1;
		subMenu = 0;
	}

	if(clearLog)
	{
		// Clear the log area pixels and color to green
		plat_gfxFill(0, 25, 0, 15, 24*8);
	}

	// redraw all tiles
	for(i=0; i<64; ++i)
	{
		plat_DrawSquare(i);
	}

	// Add the A..H and 1..8 tile-keys
	for(i=0; i<8; ++i)
	{
		// Print the letters and numbers in Black on Green
		sprintf(textStr, "%c",'A'+i);
		plat_showStrXY(2+i*BOARD_PIECE_WIDTH, 0, textStr);

		sprintf(textStr, "%d",8-i);
		plat_showStrXY(0, 1+i*3, textStr);
	}
}

/*-----------------------------------------------------------------------*/
void plat_DrawSquare(char position)
{
	char index;
	char piece, color;
	char y = position / 8, x = position & 7;
	char blackWhite = !((x & 1) ^ (y & 1));

	// Draw the board square
	plat_gfxFill(blackWhite ? 0xff : 0x0, 1 + x * BOARD_PIECE_WIDTH, 8 + y * BOARD_PIECE_HEIGHT, BOARD_PIECE_WIDTH, BOARD_PIECE_HEIGHT);

	// Get the piece data to draw the piece over the tile
	piece = gChessBoard[y][x];
	color = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	
	if(piece)
	{
		index = 1;
		if((color && blackWhite) || (!color && !blackWhite))
			index = 0;
		plat_showPiece(1 + x * BOARD_PIECE_WIDTH, 8 + y * BOARD_PIECE_HEIGHT, gfxTiles[piece-1][index]);
	}
	
	// Show the attack numbers
	if(gShowAttackBoard)
	{
		char piece_value = (gChessBoard[y][x] & 0x0f);
		char piece_color = (gChessBoard[y][x] & PIECE_WHITE) >> 7;

		// Attackers (bottom left)
		sprintf(textStr, "%d",(gpAttackBoard[giAttackBoardOffset[position][0]]));
		plat_showStrXY(1+x*BOARD_PIECE_WIDTH,(y+1)*3, textStr);

		// Defenders (bottom right)
		sprintf(textStr, "%d",(gpAttackBoard[giAttackBoardOffset[position][1]]));
		plat_showStrXY(1+x*BOARD_PIECE_WIDTH+3,(y+1)*3, textStr);
		
		// Color (0 is black, 128 is white) and piece value (1=ROOK, 2=KNIGHT, 3=BISHOP, 4=QUEEN, 5=KING, 6=PAWN)
		sprintf(textStr, "%0d",piece_value);
		plat_showStrXY(1+x*BOARD_PIECE_WIDTH,1+y*3, textStr);

		// Color
		sprintf(textStr, "%d",piece_color);
		plat_showStrXY(1+x*BOARD_PIECE_WIDTH+3,1+y*3, textStr);
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	// Show Black or White
	sprintf(textStr, "%s",gszSideLabel[side]);
	plat_showStrXY(2+8*BOARD_PIECE_WIDTH, 0, textStr);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char cursor)
{
	char colSelect[6] = {0x55, 0xaa, 0x55, 0xaa, 0x55};
	char y = position / 8, x = position & 7;

	// get a pattern for the cursor
	color = colSelect[color-2];

	// If an attacker/defender cursor, swap the colors
	if(!cursor)
		color ^= 0xff;

	// for my real Atari 800 on a Commodore 1802 monitor, this is 
	// what I want.  On Altirra 3.20 with an NTSC 800 and NTSC
	// artifacting, the colors are inverted from what I want.
	// A little odd, but I am going with the real hardware.
	color ^= 0xff;

	// turn position into a board based offset
	x *= BOARD_PIECE_WIDTH;
	y *= BOARD_PIECE_HEIGHT;

	// The board is 1 line down from the top
	y += 8;

	// Draw the two vertical bars - the "cursor"
	plat_gfxFill(color, x + 1                , y, 1, BOARD_PIECE_HEIGHT);
	plat_gfxFill(color, x + BOARD_PIECE_WIDTH, y, 1, BOARD_PIECE_HEIGHT);
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char )
{
	sprintf(textStr, "%.*s",SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH),str);
	plat_showStrXY(1+(8*BOARD_PIECE_WIDTH), (SCREEN_HEIGHT-1), textStr);
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage()
{
	// Erase the message from ShowMessage
	sprintf(textStr, "%-*s",SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH)," ");
	plat_showStrXY(1+(8*BOARD_PIECE_WIDTH), (SCREEN_HEIGHT-1), textStr);
}

/*-----------------------------------------------------------------------*/
// This function can/will change the gTile and related global variables so
// caution is needed
void plat_AddToLogWin()
{
	char bot = SCREEN_HEIGHT-2, y = 1, x = 2+(8*BOARD_PIECE_WIDTH);
	char i = 0;

	// Show a log of the moves that have been played
	for(; y<=bot; ++y)
	{
		if(undo_FindUndoLine(bot-y))
		{
			frontend_FormatLogString();
			sprintf(textStr, "%-6s",gLogStrBuffer);
			plat_showStrXY(x, y, textStr);
		}
		else
		{
			sprintf(textStr, "%-*s",SCREEN_WIDTH-x," ");
			plat_showStrXY(x, y, textStr);
		}
	}
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWinTop()
{
	plat_AddToLogWin();
}

/*-----------------------------------------------------------------------*/
char plat_TimeExpired(unsigned char aTime)
{
	if(OS.rtclok[2] > aTime)
	{
		OS.rtclok[2] = 0;
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
int plat_ReadKeys(char blocking)
{
	char key = 0;
	int keyMask = 0;

	if(blocking)
	{
		while(KEY_NONE == (key = OS.ch));
	}
	else
	{
		if(KEY_NONE == (key = OS.ch))
			return 0;
	}
		
	switch(key)
	{
		case KEY_DASH | KEY_CTRL: //KEY_UP:		// Up
			keyMask |= INPUT_UP;
		break;

		case KEY_RIGHT:		// Right
			keyMask |= INPUT_RIGHT;
		break;

		case KEY_DOWN:		// Down
			keyMask |= INPUT_DOWN;
		break;

		case KEY_LEFT:		// Left
			keyMask |= INPUT_LEFT;
		break;
		
		case KEY_ESC:			// Esc
			keyMask |= INPUT_BACKUP;
		break;

		case KEY_A:		// 'a' - Show Attackers
			keyMask |= INPUT_TOGGLE_A;
		break;
		
		case KEY_B:		// 'b' - Board attacks - Show all attacks
			keyMask |= INPUT_TOGGLE_B;
		break;
		
		case KEY_D:		// 'd' - Show Defenders
			keyMask |= INPUT_TOGGLE_D;
		break;
		
		case KEY_M:		// 'm' - Menu
			keyMask |= INPUT_MENU;
		break;

		case KEY_RETURN:		// Enter
			keyMask |= INPUT_SELECT;
		break;
		
		case KEY_R:
			keyMask |= INPUT_REDO;
		break;
		
		case KEY_U:
			keyMask |= INPUT_UNDO;
		break;
		
		// default:		// Debug - show key code
		// {
		// 	char s[] = "Key:000";
		// 	s[4] = (key/100)+'0';
		// 	key -= (s[4] - '0') * 100;
		// 	s[5] = (key/10)+'0';
		// 	s[6] = (key%10)+'0';
		// 	plat_ShowMessage(s,COLOR_RED);
		// }
		// break;
	}
	
	OS.ch = KEY_NONE;
	return keyMask;
}

/*-----------------------------------------------------------------------*/
void plat_Shutdown()
{
}
