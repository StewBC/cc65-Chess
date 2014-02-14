/*
 *	plat64.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include <c64.h>
#include <conio.h>
#include <string.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "data.h"

/*-----------------------------------------------------------------------*/
// System locations
#define VIC_BASE_RAM			(0x8000)
#define SCREEN_RAM				((char*)VIC_BASE_RAM+0x0400)
#define CHARMAP_RAM				((char*)VIC_BASE_RAM+0x0800)
#define COLOUR_RAM				((char*)0xd800)
#define MEM_KRNL_PRNT			((char*)0x288)

/*-----------------------------------------------------------------------*/
#define BOARD_PIECE_WIDTH		4
#define BOARD_PIECE_HEIGHT		3
#define SCREEN_WIDTH			40
#define SCREEN_HEIGHT			25
#define COLOR_OFFSET			(int)(COLOUR_RAM - SCREEN_RAM)
#define FONT_INVERSE			0x80
#define FONT_SPACE				0x20
#define BOARD_BLOCK				(FONT_INVERSE + FONT_SPACE)
#define SCROLL_SPEED			0xf5
#define LOG_WINDOW_HEIGHT		SCREEN_HEIGHT-2
#define CHARMAP_DEST			(BOARD_BLOCK + 1)

/*-----------------------------------------------------------------------*/
// Internal function Prototype
char plat_TimeExpired(unsigned int aTime, char *timerInit);

/*-----------------------------------------------------------------------*/
// The charactres to make the [ and ] around the highlighted pieces
static char brackets[4] = {240,237,238,253};

/*-----------------------------------------------------------------------*/
// These variables hold copies of system info that needs to be restored 
// before returning back to DOS
static char sc_ddra, sc_pra, sc_vad, sc_ct2, sc_vbc, sc_vbg0, sc_vbg1, sc_vbg2, sc_mcp, sc_txt, sc_x, sc_y;
static char gColor_ram[SCREEN_HEIGHT*SCREEN_WIDTH];

/*-----------------------------------------------------------------------*/
// Called one-time to set up the platform (or computer or whatever)
void plat_Init()
{
	int i;
	char c;

	// Setting this to 0 will not show the "Quit" option in the main menu
	gReturnToOS = 1;
	
	if(gReturnToOS)
	{
		// Save these values so they can be restored
		memcpy(gColor_ram, COLOUR_RAM, SCREEN_HEIGHT*SCREEN_WIDTH);
		sc_ddra = CIA2.ddra;
		sc_pra = CIA2.pra;
		sc_vad = VIC.addr;
		sc_ct2 = VIC.ctrl2;
		sc_vbc = VIC.bordercolor;
		sc_vbg0 = VIC.bgcolor0;
		sc_vbg1 = VIC.bgcolor1;
		sc_vbg2 = VIC.bgcolor2;
		sc_mcp = *MEM_KRNL_PRNT;
		sc_x = wherex();
		sc_y = wherey();
	}
	
	// Set up a user defined font, and move the screen to the appropriate position
	CIA2.ddra |= 0x03;
	CIA2.pra = (CIA2.pra & 0xfc) | (3-(VIC_BASE_RAM / 0x4000));
	VIC.addr = ((((int)(SCREEN_RAM - VIC_BASE_RAM) / 0x0400) << 4) + (((int)(CHARMAP_RAM - VIC_BASE_RAM) / 0x0800) << 1));
	VIC.ctrl2 |= 16;
	VIC.bordercolor = VIC.bgcolor0 = COLOR_LIGHTBLUE;
	VIC.bgcolor1 = COLOR_RED;
	VIC.bgcolor2 = COLOR_GRAY3;

	*MEM_KRNL_PRNT = (int)SCREEN_RAM / 256;

	// Save and set the text color
	sc_txt = textcolor(COLOR_BLUE);
	clrscr();

	// Copy the standard font to where the redefined char font will live
	CIA1.cra = (CIA1.cra & 0xfe);
	*(char*)0x01 = *(char*)0x01 & 0xfb;
	memcpy(CHARMAP_RAM,COLOUR_RAM,256*8);
	*(char*)0x01 = *(char*)0x01 | 0x04;
	CIA1.cra = (CIA1.cra | 0x01);

	// copy the charactes to make the chess pieces font.  
	memcpy(&CHARMAP_RAM[8*CHARMAP_DEST], &gfxTiles[0][0], sizeof(gfxTiles));

	// For the second (white) set, set all 01 bits to 10 so it uses bgcolor2
	for(i=0; i<sizeof(gfxTiles); ++i)
	{
		c = ((char*)&gfxTiles)[i];
		
		if(0x01 == (c & 0x03))
		{
			c &= ~0x03;
			c |= 0x02;
		}
			
		if(0x04 == (c & 0x0c))
		{
			c &= ~0x0c;
			c |= 0x08;
		}
			
		if(0x10 == (c & 0x30))
		{
			c &= ~0x30;
			c |= 0x20;
		}
			
		if(0x40 == (c & 0xc0))
		{
			c &= ~0xc0;
			c |= 0x80;
		}
		CHARMAP_RAM[(8*CHARMAP_DEST)+sizeof(gfxTiles)+i] = c;
	}
}

/*-----------------------------------------------------------------------*/
// This is not needed on the C64
void plat_UpdateScreen()
{
}

/*-----------------------------------------------------------------------*/
// Very simple menu with a heading and a scrolling banner as a footer
char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller, *pEnd;
	int keyMask;
	char i, j, sx, sy, numMenuItems, timerInit = 0, maxLen = 0;

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

	// Show the title
	textcolor(COLOR_GREEN);
	gotoxy(sx, sy);
	cprintf(" %.*s ",maxLen, menuItems[0]);
	
	// Leave a blank line
	textcolor(COLOR_BLACK);
	gotoxy(sx, ++sy);
	for(j=0; j<maxLen+2; ++j)
		cputc(' ');
	
	// Show all the menu items
	for(i=1; i<numMenuItems; ++i)
	{
		gotoxy(sx, sy+i);
		cprintf(" %.*s ",maxLen, menuItems[i]);
	}
	
	// Pad with blank lines to menu height
	for(;i<height;++i)
	{
		gotoxy(sx, sy+i);
		for(j=0; j<maxLen+2; ++j)
			cputc(' ');
	}

	// Select the first item
	i = 1;
	do
	{
		// Highlight the selected item
		gotoxy(sx, sy+i);
		textcolor(COLOR_WHITE);
		cprintf(">%.*s<",maxLen, menuItems[i]);
		textcolor(COLOR_BLACK);
		
		// Look for user input
		keyMask = plat_ReadKeys(0);
		
		if(keyMask & INPUT_MOTION)
		{
			// selection changes so de-highlight the selected item
			gotoxy(sx, sy+i);
			cprintf(" %.*s ",maxLen, menuItems[i]);
			
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
		gotoxy(sx,sy+height);
		textcolor(COLOR_CYAN);
		cprintf(" %.*s ",maxLen, pScroller);
		
		// Wrap the message if needed
		if((pEnd - pScroller) < maxLen-1)
		{
			gotoxy(sx+(pEnd-pScroller)+1,sy+height);
			cprintf(" %.*s ",maxLen-(pEnd - pScroller)-1, scroller);
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
	char i;
	
	if(clearLog)
	{
		for(i=0; i<25; ++i)
			memset(SCREEN_RAM+1+(i*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), FONT_SPACE, SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH));
	}
	
	// redraw all tiles
	for(i=0; i<64; ++i)
		plat_DrawSquare(i);
	
	// Add the A..H and 1..8 tile-keys
	textcolor(COLOR_GREEN);
	for(i=0; i<8; ++i)
	{
		gotoxy(3+i*BOARD_PIECE_WIDTH,0);
		cprintf("%c",'A'+i);
		gotoxy(0,2+i*BOARD_PIECE_HEIGHT);
		cprintf("%d",8-i);
	}
}

/*-----------------------------------------------------------------------*/
// Draw a tile with background and piece on it for positions 0..63
void plat_DrawSquare(char position)
{
	char piece, color, dx, dy;
	char y = position / 8, x = position & 7;
	char* drawDest = (char*)SCREEN_RAM+SCREEN_WIDTH+(y*SCREEN_WIDTH*BOARD_PIECE_HEIGHT)+(x*BOARD_PIECE_WIDTH)+1;
	char blackWhite = !((x & 1) ^ (y & 1));
	
	if(blackWhite)
		color = COLOR_WHITE;
	else
		color = COLOR_BLACK;
	
	// Unsofisticated draw			
	for(dy=0; dy<BOARD_PIECE_HEIGHT; ++dy)
	{
		for(dx=0; dx<BOARD_PIECE_WIDTH; ++dx)
		{
			*drawDest = BOARD_BLOCK;
			*(drawDest+COLOR_OFFSET) = 8+color; // Color's > 8 are multi-color mode colors
			drawDest += 1;
		}
		drawDest += (SCREEN_WIDTH - BOARD_PIECE_WIDTH);
	}

	// Show the attack numbers
	if(gShowAttackBoard)
	{
		textcolor(COLOR_RED);
		gotoxy(1+x*BOARD_PIECE_WIDTH,(y+1)*BOARD_PIECE_HEIGHT);
		cprintf("%d",(gpAttackBoard[giAttackBoardOffset[position][0]]));
		textcolor(COLOR_YELLOW);
		gotoxy(1+x*BOARD_PIECE_WIDTH+3,(y+1)*BOARD_PIECE_HEIGHT);
		cprintf("%d",(gpAttackBoard[giAttackBoardOffset[position][1]]));
		gotoxy(1+x*BOARD_PIECE_WIDTH,1+y*BOARD_PIECE_HEIGHT);
		cprintf("%02X",gChessBoard[y][x]);
		gotoxy(1+x*BOARD_PIECE_WIDTH+3,1+y*BOARD_PIECE_HEIGHT);
		cprintf("%d",(gChessBoard[y][x]&PIECE_WHITE)>>7);
	}
	
	drawDest -= ((BOARD_PIECE_HEIGHT * SCREEN_WIDTH)-1);
	
	// Get the piece data to draw the piece over the tile
	piece = gChessBoard[y][x];
	color = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	
	if(piece)
	{
		// The white pieces are 36 characters after the start of the black pieces 
		// (6 pieces at 6 characters each to draw a piece)
		if(color)
			color = 36;
			
		color += CHARMAP_DEST+(piece-1)*6;

		drawDest[0] = color;
		drawDest[1] = color+3;
		drawDest[SCREEN_WIDTH] = color+1;
		drawDest[SCREEN_WIDTH+1] = color+4;
		drawDest[2*SCREEN_WIDTH] = color+2;
		drawDest[2*SCREEN_WIDTH+1] = color+5;
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	gotoxy(2+8*BOARD_PIECE_WIDTH,0);
	textcolor(side);
	cprintf("%s",gszSideLabel[side]);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color)
{
	char y = position / 8, x = position & 7;
	
	char *drawDest = (char*)SCREEN_RAM+SCREEN_WIDTH+(y*SCREEN_WIDTH*BOARD_PIECE_HEIGHT)+(x*BOARD_PIECE_WIDTH)+1;
	
	drawDest[COLOR_OFFSET] = color;
	drawDest[COLOR_OFFSET+SCREEN_WIDTH] = color;
	drawDest[COLOR_OFFSET+2*SCREEN_WIDTH] = color;
	
	drawDest[COLOR_OFFSET+BOARD_PIECE_WIDTH-1] = color;
	drawDest[COLOR_OFFSET+SCREEN_WIDTH+BOARD_PIECE_WIDTH-1] = color;
	drawDest[COLOR_OFFSET+2*SCREEN_WIDTH+BOARD_PIECE_WIDTH-1] = color;
	
	drawDest[0] = brackets[0];
	drawDest[2*SCREEN_WIDTH] = brackets[1];
	
	drawDest[BOARD_PIECE_WIDTH-1] = brackets[2];
	drawDest[2*SCREEN_WIDTH+BOARD_PIECE_WIDTH-1] = brackets[3];
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char color)
{
	gotoxy(1+(8*BOARD_PIECE_WIDTH),SCREEN_HEIGHT-1);
	textcolor(color);
	cprintf("%.*s",SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH),str);
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage()
{
	memset(SCREEN_RAM+1+(SCREEN_HEIGHT-1)*SCREEN_WIDTH+8*BOARD_PIECE_WIDTH,FONT_SPACE,SCREEN_WIDTH-1-8*BOARD_PIECE_WIDTH);
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWin()
{
	char i;

	// Scroll the log up
	for(i=2; i<SCREEN_HEIGHT-1; ++i)
	{
		memcpy(SCREEN_RAM+1+((i-1)*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), SCREEN_RAM+1+(i*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH));
		memcpy(COLOR_OFFSET+SCREEN_RAM+1+((i-1)*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), COLOR_OFFSET+SCREEN_RAM+1+(i*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH));
	}
	memset(SCREEN_RAM+1+(SCREEN_HEIGHT-2)*SCREEN_WIDTH+8*BOARD_PIECE_WIDTH,FONT_SPACE,SCREEN_WIDTH-1-8*BOARD_PIECE_WIDTH);

	// format and draw the information to the bottom of the log area
	frontend_FormatLogString();
	gotoxy(2+(8*BOARD_PIECE_WIDTH),SCREEN_HEIGHT-2);
	textcolor(gColor[0] ? HCOLOR_WHITE : HCOLOR_BLACK);
	cprintf("%.*s",SCREEN_WIDTH-2-(8*BOARD_PIECE_WIDTH),gLogStrBuffer);
}

/*-----------------------------------------------------------------------*/
// Important note about this function is that it alters the gTile...
// global data trackers so beware when calling it
void plat_AddToLogWinTop()
{
	char i;
	
	// Scroll the log down
	for(i=SCREEN_HEIGHT-2; i>1; --i)
	{
		memcpy(SCREEN_RAM+1+(i*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), SCREEN_RAM+1+((i-1)*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH));
		memcpy(COLOR_OFFSET+SCREEN_RAM+1+(i*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), COLOR_OFFSET+SCREEN_RAM+1+((i-1)*SCREEN_WIDTH)+(8*BOARD_PIECE_WIDTH), SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH));
	}
	memset(SCREEN_RAM+1+SCREEN_WIDTH+8*BOARD_PIECE_WIDTH,FONT_SPACE,SCREEN_WIDTH-1-8*BOARD_PIECE_WIDTH);

	// If an older entry is there to become visible, add it at the top of the log
	if(undo_FindUndoLine(SCREEN_HEIGHT-3))
	{
		frontend_FormatLogString();
		gotoxy(2+(8*BOARD_PIECE_WIDTH),1);
		textcolor(gColor[0] ? HCOLOR_WHITE : HCOLOR_BLACK);
		cprintf("%.*s",SCREEN_WIDTH-2-(8*BOARD_PIECE_WIDTH),gLogStrBuffer);
	}
}

/*-----------------------------------------------------------------------*/
// Use timer B to time a duration
char plat_TimeExpired(unsigned int aTime, char *timerInit)
{
	if(!*timerInit || (CIA1.tb_lo < aTime))
	{
		*timerInit = 1;
		
		CIA1.crb &= 0xfe;
		CIA1.tb_lo = 0xff;
		CIA1.tb_hi = 0xff;
		CIA1.crb |= 0x41;
		
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
// Restore the C64 to the state it was in before RUN was typed.  Only
// ever gets called if gReturnToOS is true
void plat_Shutdown()
{
	CIA2.ddra		= sc_ddra;
	CIA2.pra		= sc_pra;
	VIC.addr		= sc_vad;
	VIC.ctrl2		= sc_ct2;
	VIC.bordercolor = sc_vbc;
	VIC.bgcolor0	= sc_vbg0;
	VIC.bgcolor1	= sc_vbg1;
	VIC.bgcolor2	= sc_vbg2;
	*MEM_KRNL_PRNT	= sc_mcp;
	CIA1.crb		&= 0xfe;
	CIA1.tb_lo		= 0xff;
	CIA1.tb_hi		= 0xff;
	CIA1.crb		= 0x8;
	
	textcolor(sc_txt);
	gotoxy(sc_x, sc_y);
	
	memcpy(COLOUR_RAM, gColor_ram, SCREEN_HEIGHT*SCREEN_WIDTH);
	
	// didn't want to make a new crt0.s just for this but if the C64
	// was aleady in lowercase before run, then this is wrong ;)
	__asm__("lda #142");
	__asm__("jsr $ffd2");
}
