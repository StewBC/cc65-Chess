/*
 *    platPlus4.c
 *    cc65 Chess
 *
 *    Created by Stefan Wessels, February 2014.
 *
 */

#include <plus4.h>
#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"
#include "dataPlus4.h"

/*-----------------------------------------------------------------------*/
// System locations					
#define BITMAP_ADDRESS          (0xA000)
#define LUMINANCE_ADDERSS       (0x0800)
#define COLOR_ADDERSS           (LUMINANCE_ADDERSS + 0x0400)
#define CHARMAP_ROM				(0xD400)
#define CHARMAP_RAM				(BITMAP_ADDRESS + 0x2000)

/*-----------------------------------------------------------------------*/
#define BOARD_PIECE_WIDTH           4
#define BOARD_PIECE_HEIGHT          3
#define SCREEN_WIDTH                40
#define SCREEN_HEIGHT               25
#define COLOR_OFFSET                0
#define SCROLL_SPEED                0x00f0

/*-----------------------------------------------------------------------*/
// Internal function Prototype
char plat_TimeExpired(unsigned int aTime, char *timerInit);

/*-----------------------------------------------------------------------*/
// Function Prototype for functions in hires.s
void plat_gfxFill(char fill, char x, char y, char w, char h);
void plat_colorFill(char color, char x, char y, char w, char h);
void plat_showStrXY(char x, char y, char *str);
void plat_colorStringXY(char color, char x, char y, char *str);
void plat_showPiece(char x, char Y, const char *src);

/*-----------------------------------------------------------------------*/
// Local storage
static char t_bcol, t_bgco, t_colo, t_vscr, t_hscr, t_misc, t_vide;
char textStr[41];
static char subMenu;

/*-----------------------------------------------------------------------*/
// Called one-time to set up the platform (or computer or whatever)
void plat_Init()
{
    // Clearly something I don't understand here.  If I make the loop counter an
    // int, or even another char I define, this all stops working.
    char a, b, c;
    for(gReturnToOS = 0; gReturnToOS < 255; gReturnToOS++) {
        TED.enable_rom = 1;
        a = *((char*)(CHARMAP_ROM+gReturnToOS));
        b = *((char*)(CHARMAP_ROM+256+gReturnToOS));
        c = *((char*)(CHARMAP_ROM+512+gReturnToOS));
        TED.enable_ram = 0;
        *((char*)(CHARMAP_RAM+gReturnToOS)) = a;
        *((char*)(CHARMAP_RAM+256+gReturnToOS)) = b;
        *((char*)(CHARMAP_RAM+512+gReturnToOS)) = c;
    }

    // Setting this to 0 will not show the "Quit" option in the main menu
    gReturnToOS = 1;

    if(gReturnToOS)
    {
        // Save these values so they can be restored
        t_bcol = TED.bordercolor;
        t_bgco = TED.bgcolor;
        t_colo = TED.color1;
        t_vscr = TED.vscroll;
        t_hscr = TED.hscroll;
        t_misc = TED.misc;
        t_vide = TED.video_addr;
    }

    // Move the cursor to the lower right since cursor(0) doesn't seem to work.
    gotoxy(39,24);
    // This doesn't appear to work
    cursor(0);

    TED.bordercolor = COLOR_GREEN;
    TED.bgcolor = BCOLOR_GREEN | CATTR_LUMA4;
    TED.color1 = BCOLOR_BLACK;
    TED.vscroll &= ~0b00010000;

    // Set the location of the bitmap
    TED.misc = (TED.misc & 0b11000001) | (BITMAP_ADDRESS >> 0x0A);
    // Set the location of the luminance and color
    TED.video_addr = (TED.video_addr & 0b00000111) | (LUMINANCE_ADDERSS >> 0x08);

    // Clear all pixels on the screen
    plat_gfxFill(0,0,0,40,25);

    // set the base screen color on green
    // memset(SCREEN_RAM, COLOR_WHITE<<4|COLOR_GREEN, 0x400);
    memset((char*)LUMINANCE_ADDERSS, CATTR_LUMA3 | CATTR_LUMA7>>4, 0x400);
    memset((char*)COLOR_ADDERSS, BCOLOR_WHITE<<4 | BCOLOR_GREEN, 0x400);

    // Show the welcome text
    strcpy(textStr, "Commodore 64 graphics version, 2020.");
    plat_showStrXY(2,SCREEN_HEIGHT/2-1, gszAbout);
    plat_showStrXY(2,SCREEN_HEIGHT/2+1,textStr);
    plat_colorStringXY(COLOR_BLACK<<4|COLOR_GREEN,2,SCREEN_HEIGHT/2+1,textStr);

    // Set the color for the black king
    plat_colorFill(COLOR_BLACK<<4|COLOR_GREEN ,SCREEN_WIDTH/2 - 1, SCREEN_HEIGHT/2 - 6, 4, 3);
    // Show the welcome kings (black and white, solid versions)
    plat_showPiece(SCREEN_WIDTH/2 - 1, SCREEN_HEIGHT/2 - 6, gfxTiles[KING-1][1]);
    plat_showPiece(SCREEN_WIDTH/2 - 1, SCREEN_HEIGHT/2 + 4, gfxTiles[KING-1][1]);

    // hack to make the menu not flash (no when dimming into sub-menus)
    subMenu = 0;

    // Un-blank the screen
    TED.vscroll |= 0b00110000;

    plat_ReadKeys(1);    
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
    char i, sx, sy, numMenuItems, timerInit = 0, maxLen = 0;

    // Darken the chessboard so the menu "pops"
    if(!subMenu++)
        plat_colorFill(COLOR_GRAY1<<4,1,1,32,24);

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
    plat_gfxFill(0xAA             , sx-1       , sy-1       , maxLen+4 , 1        );
    plat_gfxFill(0xAA             , sx-1       , sy         , 1        , height+3 );
    plat_gfxFill(0xAA             , sx+maxLen+2, sy         , 1        , height+3 );
    plat_gfxFill(0xAA             , sx         , sy+height+2, maxLen+2 , 1        );

    plat_colorFill(COLOR_YELLOW<<4, sx-1       , sy-1       , maxLen+4 , 1        );
    plat_colorFill(COLOR_YELLOW<<4, sx-1       , sy         , 1        , height+3 );
    plat_colorFill(COLOR_YELLOW<<4, sx+maxLen+2, sy         , 1        , height+3 );
    plat_colorFill(COLOR_YELLOW<<4, sx         , sy+height+2, maxLen+2 , 1        );


    // Show the title
    sprintf(textStr, " %.*s ",38, menuItems[0]);
    plat_showStrXY(sx, sy, textStr);
    plat_colorStringXY(COLOR_YELLOW<<4|COLOR_BLUE, sx, sy, textStr);

    // Leave a blank line
    sprintf(textStr, "%-*s", maxLen+2," ");
    plat_showStrXY(sx, ++sy, textStr);
    plat_colorStringXY(COLOR_BLUE, sx, sy, textStr);
    
    // Show all the menu items
    for(i=1; i<numMenuItems; ++i)
    {
        sprintf(textStr, " %.*s ",maxLen, menuItems[i]);
        plat_showStrXY(sx, sy+i, textStr);
        plat_colorStringXY(COLOR_GRAY2<<4|COLOR_BLUE, sx, sy+i, textStr);
    }
    
    // Pad with blank lines to menu height
    for(;i<height;++i)
    {
        sprintf(textStr, "%-*s", maxLen+2," ");
        plat_showStrXY(sx, sy+i, textStr);
        plat_colorStringXY(COLOR_BLUE, sx, sy+i, textStr);
    }
    
    // Color the scroller area once
    plat_colorStringXY(COLOR_CYAN<<4|COLOR_BLUE, sx, sy+height, textStr);

    // Select the first item
    i = 1;
    do
    {
        // Highlight the selected item
        sprintf(textStr, ">%.*s<",maxLen, menuItems[i]);
        plat_showStrXY(sx, sy+i, textStr);
        plat_colorStringXY(COLOR_WHITE<<4|COLOR_BLUE, sx, sy+i, textStr);
        
        // Look for user input
        keyMask = plat_ReadKeys(0);
        
        if(keyMask & INPUT_MOTION)
        {
            // selection changes so de-highlight the selected item
            sprintf(textStr, " %.*s ",maxLen, menuItems[i]);
            plat_showStrXY(sx, sy+i, textStr);
            plat_colorStringXY(COLOR_GRAY2<<4|COLOR_BLUE, sx, sy+i, textStr);

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

    // Not in a menu when drawBoard is called
    subMenu = 0;

    if(clearLog)
    {
        // Clear the log area pixels and color to green
        plat_gfxFill(0,33,0,7,25);
        plat_colorFill(COLOR_GREEN,33,0,7,25);
    }

    // set the left and top margin row/col to clear & green
    plat_colorFill(COLOR_GREEN,0,0,33,1);
    plat_colorFill(COLOR_GREEN,0,0,1,25);
    
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
        plat_colorStringXY(COLOR_BLACK<<4|COLOR_GREEN,2+i*BOARD_PIECE_WIDTH, 0, textStr);

        sprintf(textStr, "%d",8-i);
        plat_showStrXY(0, 2+i*BOARD_PIECE_HEIGHT, textStr);
        plat_colorStringXY(COLOR_BLACK<<4|COLOR_GREEN,0, 2+i*BOARD_PIECE_HEIGHT, textStr);
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
    plat_gfxFill(blackWhite ? 0xff : 0x0, 1 + x * BOARD_PIECE_WIDTH, 1 + y * BOARD_PIECE_HEIGHT, BOARD_PIECE_WIDTH, BOARD_PIECE_HEIGHT);
    plat_colorFill(COLOR_WHITE<<4     , 1 + x * BOARD_PIECE_WIDTH, 1 + y * BOARD_PIECE_HEIGHT, BOARD_PIECE_WIDTH, BOARD_PIECE_HEIGHT);
    
    // Get the piece data to draw the piece over the tile
    piece = gChessBoard[y][x];
    color = piece & PIECE_WHITE;
    piece &= PIECE_DATA;
    
    if(piece)
    {
        index = 1;
        if((color && blackWhite) || (!color && !blackWhite))
            index = 0;
        plat_showPiece(1 + x * BOARD_PIECE_WIDTH, y * BOARD_PIECE_HEIGHT + 1, gfxTiles[piece-1][index]);
    }

    // Show the attack numbers
    if(gShowAttackBoard)
    {
        char piece_value = (gChessBoard[y][x] & 0x0f);
        char piece_color = (gChessBoard[y][x] & PIECE_WHITE) >> 7;

        // Attackers (bottom left)
        sprintf(textStr, "%d",(gpAttackBoard[giAttackBoardOffset[position][0]]));
        plat_showStrXY(1+x*BOARD_PIECE_WIDTH,(y+1)*BOARD_PIECE_HEIGHT, textStr);
        plat_colorStringXY((piece_color ? COLOR_RED : COLOR_GREEN)<<4,1+x*BOARD_PIECE_WIDTH,(y+1)*BOARD_PIECE_HEIGHT, textStr);

        // Defenders (bottom right)
        sprintf(textStr, "%d",(gpAttackBoard[giAttackBoardOffset[position][1]]));
        plat_showStrXY(1+x*BOARD_PIECE_WIDTH+3,(y+1)*BOARD_PIECE_HEIGHT, textStr);
        plat_colorStringXY((!piece_color ? COLOR_RED : COLOR_GREEN)<<4,1+x*BOARD_PIECE_WIDTH+3,(y+1)*BOARD_PIECE_HEIGHT, textStr);
        
        // Color (0 is black, 128 is white) and piece value (1=ROOK, 2=KNIGHT, 3=BISHOP, 4=QUEEN, 5=KING, 6=PAWN)
        sprintf(textStr, "%0d",piece_value);
        plat_showStrXY(1+x*BOARD_PIECE_WIDTH,1+y*BOARD_PIECE_HEIGHT, textStr);
        plat_colorStringXY(COLOR_PURPLE<<4,1+x*BOARD_PIECE_WIDTH,1+y*BOARD_PIECE_HEIGHT, textStr);

        // Color
        sprintf(textStr, "%d",piece_color);
        plat_showStrXY(1+x*BOARD_PIECE_WIDTH+3,1+y*BOARD_PIECE_HEIGHT, textStr);
        plat_colorStringXY((piece_color ? COLOR_WHITE : COLOR_GRAY1)<<4,1+x*BOARD_PIECE_WIDTH+3,1+y*BOARD_PIECE_HEIGHT, textStr);
    }
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
    // Show Black or White
    sprintf(textStr, "%s",gszSideLabel[side]);
    plat_showStrXY(2+8*BOARD_PIECE_WIDTH, 0, textStr);
    plat_colorStringXY((side ? COLOR_WHITE : COLOR_BLACK)<<4|COLOR_GREEN, 2+8*BOARD_PIECE_WIDTH, 0, textStr);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char)
{
    char y = position / 8, x = position & 7;
    char white = !((x & 1) ^ (y & 1));
    
    // White pieces need the foreground color set, black the background color
    if(white)
        color <<= 4;

    // turn position into a board based offset
    x *= BOARD_PIECE_WIDTH;
    y *= BOARD_PIECE_HEIGHT;

    // The board is 1 line down from the top
    y++;

    // Draw the two vertical bars - the "cursor"
    plat_colorFill(color, x + 1                , y, 1, BOARD_PIECE_HEIGHT);
    plat_colorFill(color, x + BOARD_PIECE_WIDTH, y, 1, BOARD_PIECE_HEIGHT);

}

/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char)
{
    // Always an error message - illegal move or no more undo/redo
    sprintf(textStr, "%.*s",SCREEN_WIDTH-1-(8*BOARD_PIECE_WIDTH),str);
    plat_showStrXY(1+(8*BOARD_PIECE_WIDTH), SCREEN_HEIGHT-1, textStr);
    plat_colorStringXY(COLOR_RED<<4|COLOR_GREEN, 1+(8*BOARD_PIECE_WIDTH), SCREEN_HEIGHT-1, textStr);
}

/*-----------------------------------------------------------------------*/
void plat_ClearMessage()
{
    // Erase the message from ShowMessage
    sprintf(textStr, "%-*s",SCREEN_WIDTH-1-8*BOARD_PIECE_WIDTH," ");
    plat_showStrXY((8*BOARD_PIECE_WIDTH)+1, (8*BOARD_PIECE_HEIGHT), textStr);
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
            plat_showStrXY(x, y, textStr);
            plat_colorStringXY((gColor[0] ? COLOR_WHITE : COLOR_BLACK)<<4|COLOR_GREEN, x, y, textStr);
        }
        else
        {
            sprintf(textStr, "%-*s",SCREEN_WIDTH-x," ");
            plat_showStrXY(x, y, textStr);
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
// Use timer 1 to time a duration
char plat_TimeExpired(unsigned int aTime, char *timerInit)
{
    static unsigned int h = 0xffff;
    unsigned int t = *(int*)&TED.t1_lo;
    if(!*timerInit || t < aTime || t > h)
    {
        *timerInit = 1;

        TED.t1_lo = 0xFF;
        TED.t1_hi = 0xFF;
        h = 0xffff;

        return 1;
    }
    else
    {
        h = t;
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
        case 145:        // Up
            keyMask |= INPUT_UP;
        break;

        case 29:        // Right
            keyMask |= INPUT_RIGHT;
        break;

        case 17:        // Down
            keyMask |= INPUT_DOWN;
        break;

        case 157:        // Left
            keyMask |= INPUT_LEFT;
        break;
        
        case 3:            // Esc
            keyMask |= INPUT_BACKUP;
        break;

        case 65:        // 'a' - Show Attackers
            keyMask |= INPUT_TOGGLE_A;
        break;
        
        case 66:        // 'b' - Board attacks - Show all attacks
            keyMask |= INPUT_TOGGLE_B;
        break;
        
        case 68:        // 'd' - Show Defenders
            keyMask |= INPUT_TOGGLE_D;
        break;
        
        case 77:        // 'm' - Menu
            keyMask |= INPUT_MENU;
        break;

        case 13:        // Enter
            keyMask |= INPUT_SELECT;
        break;
        
        case 82:
            keyMask |= INPUT_REDO;
        break;
        
        case 85:
            keyMask |= INPUT_UNDO;
        break;
        
        // default:        // Debug - show key code
        // {
        //    char s[] = "Key:000";
        //    s[4] = (key/100)+'0';
        //    key -= (s[4] - '0') * 100;
        //    s[5] = (key/10)+'0';
        //    s[6] = (key%10)+'0';
        //    plat_ShowMessage(s,COLOR_RED);
        // }
        break;
    }
    
    return keyMask;
}

/*-----------------------------------------------------------------------*/
// Only gets called if gReturnToOS is true, which it isn't
void plat_Shutdown()
{
    // restore Plus4 specific hardware values
    TED.bordercolor = t_bcol;
    TED.bgcolor     = t_bgco;
    TED.color1      = t_colo;
    TED.vscroll     = t_vscr;
    TED.hscroll     = t_hscr;
    TED.misc        = t_misc;
    TED.video_addr  = t_vide;
    clrscr();

    // Bank in the ROM
    TED.enable_rom = 1;
    cursor(1);
}
