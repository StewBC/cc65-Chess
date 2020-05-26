/*
 *	platTerm.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include <curses.h>
#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "globals.h"
#include "undo.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
// Internal function Prototype
char plat_TimeExpired(unsigned int aTime);

/*-----------------------------------------------------------------------*/
// These are read from the curses library but I assume at least 24 rows
// and 55 cols
int SCREEN_HEIGHT, SCREEN_WIDTH;
int LOG_WINDOW_HEIGHT;

/*-----------------------------------------------------------------------*/
#define SCROLL_SPEED 150000
#define BOARD_PIECE_WIDTH	6
#define BOARD_PIECE_HEIGHT	3

/*-----------------------------------------------------------------------*/
// Names of the pieces NONE, Rook, knight, Bishop, Queen, King, pawn
static const char sc_pieces[] = {'\0','R','k','B','Q','K','p'};

/*-----------------------------------------------------------------------*/
void plat_Init()
{
	// Curses init
	initscr();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	nonl();
	cbreak();
	noecho();
	timeout(0);
	curs_set(0);
	getmaxyx(stdscr, SCREEN_HEIGHT, SCREEN_WIDTH) ;
	LOG_WINDOW_HEIGHT = SCREEN_HEIGHT - 2;

	if(has_colors() != FALSE)
	{
		start_color();
		assume_default_colors(COLOR_WHITE, COLOR_BLACK);
		init_pair(1, COLOR_BLACK, COLOR_YELLOW);
		init_pair(2, COLOR_WHITE, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_RED, COLOR_BLACK);
		init_pair(5, COLOR_CYAN, COLOR_BLACK);
		init_pair(6, COLOR_BLUE, COLOR_BLACK);
		init_pair(7, COLOR_WHITE, COLOR_WHITE);
		init_pair(8, COLOR_BLACK, COLOR_BLACK);
	}

	// Setting this to 0 will not show the "Quit" option in the main menu
	gReturnToOS = 1;
}

/*-----------------------------------------------------------------------*/
void plat_UpdateScreen()
{
	refresh();
}

/*-----------------------------------------------------------------------*/
// Very simple menu with a heading and a scrolling banner as a footer
char plat_Menu(char **menuItems, char height, char *scroller)
{
	static char *prevScroller, *pScroller;
	char *pEnd;
	int keyMask;
	char i, j, sx, sy, numMenuItems, maxLen = 0;
	
	if(prevScroller != scroller)
	{
		prevScroller = scroller;
		pScroller = scroller;
	}
	pEnd = scroller + strlen(scroller);
	
	for(numMenuItems=0; menuItems[numMenuItems]; ++numMenuItems)
	{
		char len = strlen(menuItems[numMenuItems]);
		if(len > maxLen)
			maxLen = len;
	}
	sy = MAX_SIZE(0, ((8*BOARD_PIECE_HEIGHT) / 2) - (height / 2) - 1);
	sx = MAX_SIZE(0, ((8*BOARD_PIECE_WIDTH) / 2) - (maxLen / 2) - 1);
	maxLen = MIN_SIZE((8*BOARD_PIECE_WIDTH)-2, maxLen);
	
	color_set(3,0);
	move(sy,sx);
	printw(" %.*s ",maxLen, menuItems[0]);
	color_set(1,0);
	move(++sy, sx);
	for(j=0; j<maxLen+2; ++j)
		printw(" ");
	
	for(i=1; i<numMenuItems; ++i)
	{
		move(sy+i, sx);
		printw(" %.*s ",maxLen, menuItems[i]);
	}
	
	for(;i<height;++i)
	{
		move(sy+i, sx);
		for(j=0; j<maxLen+2; ++j)
			printw(" ");
	}
	
	i = 1;
	do
	{
		move(sy+i,sx);
		attron(WA_REVERSE);
		color_set(2,0);
		printw(">%.*s<",maxLen, menuItems[i]);
		attroff(WA_REVERSE);
		color_set(1,0);
		keyMask = plat_ReadKeys(0);
		if(keyMask & INPUT_MOTION)
		{
			move(sy+i,sx);
			printw(" %.*s ",maxLen, menuItems[i]);
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
		
		move(sy+height,sx);
		color_set(5,0);
		printw(" %.*s ",maxLen, pScroller);
		if((pEnd - pScroller) < maxLen-1)
		{
			move(sy+height,sx+(pEnd-pScroller)+1);
			printw(" %.*s ",maxLen-(pEnd - pScroller)-1, scroller);
		}
		
		if(plat_TimeExpired(SCROLL_SPEED))
		{
			++pScroller;
			if(!*pScroller)
				pScroller = scroller;
		}
	} while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);
	
	if(keyMask & INPUT_BACKUP)
		return 0;
	
	return i;
}

/*-----------------------------------------------------------------------*/
void plat_DrawBoard(char clearLog)
{
	char i;
	
	if(clearLog)
		erase();
	
	for(i=0; i<64; ++i)
		plat_DrawSquare(i);
}

/*-----------------------------------------------------------------------*/
void plat_DrawSquare(char position)
{
	char piece, color, dx, dy;
	char y = position / 8, x = position & 7;
	char blackWhite = !((x & 1) ^ (y & 1));
	
	if(blackWhite)
		color = 7;
	else
		color = 8;
	
	for(dy=0; dy<BOARD_PIECE_HEIGHT; ++dy)
	{
		for(dx=0; dx<BOARD_PIECE_WIDTH; ++dx)
		{
			move(dy+y*BOARD_PIECE_HEIGHT,dx+x*BOARD_PIECE_WIDTH);
			color_set(color,0);
			printw(" ");
		}
	}
	
	// Show the attack numbers
	if(gShowAttackBoard)
	{
		color_set(1, 0);
		move(y*BOARD_PIECE_HEIGHT+2, x*BOARD_PIECE_WIDTH);
		printw("%d",(gpAttackBoard[giAttackBoardOffset[position][0]]));
		color_set(2,0);
		move(y*BOARD_PIECE_HEIGHT+2, x*BOARD_PIECE_WIDTH+5);
		printw("%d",(gpAttackBoard[giAttackBoardOffset[position][1]]));
		move(y*BOARD_PIECE_HEIGHT, x*BOARD_PIECE_WIDTH);
		printw("%02X",gChessBoard[y][x]);
		move(y*BOARD_PIECE_HEIGHT, x*BOARD_PIECE_WIDTH+5);
		printw("%d",(gChessBoard[y][x]&PIECE_WHITE)>>7);
	}
	
	piece = gChessBoard[y][x];
	color = piece & PIECE_WHITE;
	piece &= PIECE_DATA;
	
	if(piece)
	{
		move(y*BOARD_PIECE_HEIGHT+(BOARD_PIECE_HEIGHT/2),x*BOARD_PIECE_WIDTH+(BOARD_PIECE_WIDTH/2));
		color_set(color?2:1,0);
		printw("%c",sc_pieces[piece]);
	}
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
	move(0, 2+8*BOARD_PIECE_WIDTH);
	color_set(side?2:1, 0);
	printw("%s",gszSideLabel[side]);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char cursor)
{
	char y = (BOARD_PIECE_HEIGHT/2)+1+BOARD_PIECE_HEIGHT*((position / 8)), x = (BOARD_PIECE_WIDTH/2)+BOARD_PIECE_WIDTH*((position & 7));
	move(y,x);
	color_set(color,0);
	printw(cursor?"*":"!");
}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char color)
{
	move((8*BOARD_PIECE_HEIGHT)-1, 1+(8*BOARD_PIECE_WIDTH));
	color_set(color,0);
	printw("%.*s",SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH),str);
	clrtoeol();
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage()
{
	move((8*BOARD_PIECE_HEIGHT)-1, 1+(8*BOARD_PIECE_WIDTH));
	clrtoeol();
}

/*-----------------------------------------------------------------------*/
// This function can/will change the gTile and related global variables so
// caution is needed
void plat_AddToLogWin()
{
	char bot = (8*BOARD_PIECE_HEIGHT)-2, y = 1, x = 1+(8*BOARD_PIECE_WIDTH);

	for(; y<=bot; ++y)
	{
		move(y, x);
		if(undo_FindUndoLine(bot-y))
		{
			frontend_FormatLogString();
			color_set(gColor[0]+1,0);
			printw("%.*s",SCREEN_WIDTH-1-x,gLogStrBuffer);
		}
		clrtoeol();
	}
}

/*-----------------------------------------------------------------------*/
void plat_AddToLogWinTop()
{
	plat_AddToLogWin();
}

/*-----------------------------------------------------------------------*/
char plat_TimeExpired(unsigned int aTime)
{
	static struct timeval sst_store;
	static int si_init = 0;
	struct timeval now;
	gettimeofday(&now, NULL);
	if(!si_init || (MAX_SIZE(now.tv_usec,sst_store.tv_usec) - MIN_SIZE(now.tv_usec,sst_store.tv_usec) > SCROLL_SPEED))
	{
		si_init = 1;
		sst_store = now;
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
		timeout(-1) ;
		key = getch();
		timeout(0);
	}
	else
	{
		key = getch();
	}
	
	switch(key)
	{
		case 3:		// Up
			keyMask |= INPUT_UP;
			break;
			
		case 5:		// Right
			keyMask |= INPUT_RIGHT;
			break;
			
		case 2:		// Down
			keyMask |= INPUT_DOWN;
			break;
			
		case 4:		// Left
			keyMask |= INPUT_LEFT;
			break;
			
		case 27:			// Esc
			keyMask |= INPUT_BACKUP;
			break;
			
		case 'a':		// 'a' - Show Attackers
			keyMask |= INPUT_TOGGLE_A;
			break;
			
		case 'b':		// 'b' - Board attacks - Show all attacks
			keyMask |= INPUT_TOGGLE_B;
			break;
			
		case 'd':		// 'd' - Show Defenders
			keyMask |= INPUT_TOGGLE_D;
			break;
			
		case 'm':		// 'm' - Menu
			keyMask |= INPUT_MENU;
			break;

		case 13:		// Enter
			keyMask |= INPUT_SELECT;
			break;
			
		case 'r':
			keyMask |= INPUT_REDO;
			break;
			
		case 'u':
			keyMask |= INPUT_UNDO;
			break;
			
		// default:		// Debug - show key code
		// {
		//	char s[] = "Key:000";
		//			   if(key != 255)
		//			   {
		//				   s[4] = (key/100)+'0';
		//				   key -= (s[4] - '0') * 100;
		//				   s[5] = (key/10)+'0';
		//				   s[6] = (key%10)+'0';
		//				   plat_ShowMessage(s,COLOR_RED);
		//			   }
		//			   refresh();
		// }
		//		   break;
	}
	
	return keyMask;
}

/*-----------------------------------------------------------------------*/
void plat_Shutdown()
{
	endwin();
}
