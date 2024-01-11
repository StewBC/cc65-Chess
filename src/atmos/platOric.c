/*
 *  platOric.c
 *  cc65 Chess
 *
 *  Implementation for Oric by [raxiss], May 2020
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../types.h"
#include "../globals.h"
#include "../undo.h"
#include "../frontend.h"
#include "../plat.h"

#include "platOric.h"

#define BOARD_X                 1
#define BOARD_Y                 18

#define BOARD_PIECE_WIDTH       3
#define BOARD_PIECE_HEIGHT      22

#define CHAR_HEIGHT             8
#define LOG_WINDOW_HEIGHT       23

#define SCREEN_WIDTH            40
#define SCREEN_HEIGHT           200

#define ROP_CPY                 0x40
#define ROP_INV                 0xc0

/*-----------------------------------------------------------------------*/
static char moveBuf[7];

/*-----------------------------------------------------------------------*/
extern char hires_CharSet[96][CHAR_HEIGHT];
extern char hires_Pieces[4*7*BOARD_PIECE_WIDTH*BOARD_PIECE_HEIGHT];

/*-----------------------------------------------------------------------*/
static void clrmenu(void);
static void cputs(char* s);
static void gotoxy(int x, int y);
static void revers(char flag);
static void cprintf(char* fmt, char* s);

/*-----------------------------------------------------------------------*/
#define peek(addr)          ((unsigned char*)addr)[0]
#define poke(addr, val)     {((unsigned char*)addr)[0] = val;}
#define deek(addr)          ((unsigned int*)addr)[0]
#define doke(addr, val)     {((unsigned int*)addr)[0] = val;}
#define unused(x)           (void)(x)
/*-----------------------------------------------------------------------*/
// Names of the pieces NONE, Rook, knight, Bishop, Queen, King, pawn
// static const char sc_pieces[] = {'\0','R','k','B','Q','K','p'};

/*-----------------------------------------------------------------------*/
void plat_Init(void)
{
  char i;
  unsigned int n;

  setup();
  clrmenu();

  // Show credits and wait for key press
  plat_DrawString(2,SCREEN_HEIGHT/2-10-CHAR_HEIGHT/2,
                  ROP_CPY,gszAbout);
  plat_DrawString(1,SCREEN_HEIGHT/2+10-CHAR_HEIGHT/2,
                  ROP_CPY,"Oric-1/Atmos version by [raxiss], 2020.");

  n = 0*2*7*BOARD_PIECE_HEIGHT + 0*7*BOARD_PIECE_HEIGHT + (KING-1);
  hires_Draw(SCREEN_WIDTH/2-2,SCREEN_HEIGHT/2-50-BOARD_PIECE_HEIGHT/2,
             BOARD_PIECE_WIDTH,BOARD_PIECE_HEIGHT,ROP_CPY,
             &hires_Pieces[n*BOARD_PIECE_WIDTH]);

  n = 1*2*7*BOARD_PIECE_HEIGHT + 0*7*BOARD_PIECE_HEIGHT + (KING-1);
  hires_Draw(SCREEN_WIDTH/2-2,SCREEN_HEIGHT/2+50-BOARD_PIECE_HEIGHT/2,
             BOARD_PIECE_WIDTH,BOARD_PIECE_HEIGHT,ROP_CPY,
             &hires_Pieces[n*BOARD_PIECE_WIDTH]);

  plat_ReadKeys(1);

  hires();
  // Draw the board border and
  // add the A..H and 1..8 tile-keys
  for(i=0; i<8; ++i)
  {
    for(n=0; n<BOARD_PIECE_HEIGHT; ++n)
    {
      poke(0xa000+(18+i*BOARD_PIECE_HEIGHT+n)*40,0x41);
      poke(0xa000+(18+i*BOARD_PIECE_HEIGHT+n)*40+25,0x60);
    }
    for(n=0; n<BOARD_PIECE_WIDTH; ++n)
    {
      poke(0xa000+(17+0*BOARD_PIECE_HEIGHT)*40+i*BOARD_PIECE_WIDTH+n+1,0x7f);
      poke(0xa000+(18+8*BOARD_PIECE_HEIGHT)*40+i*BOARD_PIECE_WIDTH+n+1,0x7f);
    }
    plat_DrawChar(2+i*BOARD_PIECE_WIDTH,4,ROP_CPY,i+'A');
    plat_DrawChar(26,SCREEN_HEIGHT-22-i*BOARD_PIECE_HEIGHT,ROP_CPY,i+'1');
  }

  clrmenu();

  // Setting this to 0 will not show the "Quit" option in the main menu
  gReturnToOS = 1;
}

/*-----------------------------------------------------------------------*/
char plat_Menu(char **menuItems, char height, char *scroller)
{
  int keyMask;
  char i, j;

  clrmenu();

  // Show the title or the scroller if that is more relevant.
  cputs(scroller == gszAbout ? menuItems[0] : scroller);

  // Select the first item
  i = 1;
  do
  {
    // Show all the menu items
    for(j=1; j<height; ++j)
    {
      if(!menuItems[j])
        break;

      gotoxy(((j-1)%2)*SCREEN_WIDTH/2,((j-1)/2)+1);

      // Highlight the selected item
      if(j==i)
        revers(1);

      cprintf("  %s  ",menuItems[j]);

      if(j==i)
        revers(0);
    }

    // Look for user input
    keyMask = plat_ReadKeys(1);

    if(keyMask & INPUT_MOTION)
    {
      switch(keyMask & INPUT_MOTION)
      {
        case INPUT_LEFT:
          if(!--i)
            i = j-1;
          break;

        case INPUT_RIGHT:
          if(j == ++i)
            i = 1;
          break;

        case INPUT_UP:
          if(i > 2)
            i -= 2;
          else
            i = j-1-(i-1)%2;
          break;

        case INPUT_DOWN:
          if(i < j-2)
            i += 2;
          else
            i = 1+(i-1)%2;
          break;
      }
    }
    keyMask &= (INPUT_SELECT | INPUT_BACKUP);

  } while(keyMask != INPUT_SELECT && keyMask != INPUT_BACKUP);

  clrmenu();

  // If backing out of the menu, return 0
  if(keyMask & INPUT_BACKUP)
    return 0;

  // Return the selection
  return i;
}

/*-----------------------------------------------------------------------*/
extern char hires_xpos;
extern char hires_ypos;
extern char hires_xsize;
extern char hires_ysize;
extern char hires_rop;
extern char hires_clr;
extern char* hires_src;
void hires_MaskA(void);
void hires_DrawA(void);
void hires_DrawCharA(void);
void hires_DrawClrA(void);
void hires_SelectA(void);

/*-----------------------------------------------------------------------*/
void hires_Mask(char xpos, char ypos, char xsize, char ysize, char rop)
{
  hires_xpos = xpos;
  hires_ypos = ypos;
  hires_xsize = xsize;
  hires_ysize = ysize;
  hires_rop = rop;
  hires_MaskA();
}

/*-----------------------------------------------------------------------*/
void hires_Draw(char xpos, char ypos, char xsize, char ysize, char rop, char *src)
{
  hires_xpos = xpos;
  hires_ypos = ypos;
  hires_xsize = xsize;
  hires_ysize = ysize;
  hires_rop = rop;
  hires_src = src;
  hires_DrawA();
}

/*-----------------------------------------------------------------------*/
void hires_DrawClr(char xpos, char ypos, char clr)
{
  hires_xpos = xpos;
  hires_ypos = ypos;
  hires_clr = clr;
  hires_DrawClrA();
}

/*-----------------------------------------------------------------------*/
void hires_Select(char xpos, char ypos, char xsize, char ysize, char rop, char *src)
{
  hires_xpos = xpos;
  hires_ypos = ypos;
  hires_xsize = xsize;
  hires_ysize = ysize;
  hires_rop = rop;
  hires_src = src;
  hires_SelectA();
}

/*-----------------------------------------------------------------------*/
void plat_DrawChar(char xpos, char ypos, char rop, char c)
{
  hires_xpos = xpos;
  hires_ypos = ypos;
  hires_rop = rop;
  hires_src = hires_CharSet[c-' '];
  hires_DrawCharA();
}

/*-----------------------------------------------------------------------*/
void plat_DrawString(char x, char y, char rop, char *s)
{
  while(*s)
    plat_DrawChar(x++,y, rop, *s++);
}

/*-----------------------------------------------------------------------*/
void plat_UpdateScreen(void)
{
  memset(moveBuf,0x20,6); moveBuf[6] = 0;
  plat_DrawString(SCREEN_WIDTH-6,(LOG_WINDOW_HEIGHT+1)*CHAR_HEIGHT,
                  ROP_CPY,moveBuf);

  /*
  switch(gOutcome)
  {
    case OUTCOME_INVALID:
    case OUTCOME_OK:
    case OUTCOME_CHECK:
    case OUTCOME_CHECKMATE:
    case OUTCOME_DRAW:
    case OUTCOME_STALEMATE:
    case OUTCOME_MENU:
    case OUTCOME_ABANDON:
    case OUTCOME_QUIT:
      break;
  }
  */
}

/*-----------------------------------------------------------------------*/
void plat_DrawBoard(char clearLog)
{
  int i;

  if(clearLog)
    hires_Mask(27,0,SCREEN_WIDTH-27,SCREEN_HEIGHT,ROP_CPY);

  // Redraw all tiles
  for(i=0; i<64; ++i)
    plat_DrawSquare(i);
}

/*-----------------------------------------------------------------------*/
void plat_DrawSquare(char position)
{
  char y = position / 8, x = position & 7;
  char piece = gChessBoard[y][x];
  char blackWhite = !((x & 1) ^ (y & 1));
  char pieceBlackWhite = !(piece & PIECE_WHITE);

  unsigned int idx = blackWhite*7*BOARD_PIECE_HEIGHT + (piece&PIECE_DATA);

  if(piece) idx += pieceBlackWhite*2*7*BOARD_PIECE_HEIGHT;

  hires_Draw(BOARD_X+x*BOARD_PIECE_WIDTH,BOARD_Y+y*BOARD_PIECE_HEIGHT,
             BOARD_PIECE_WIDTH,BOARD_PIECE_HEIGHT,ROP_CPY,
             &hires_Pieces[idx*BOARD_PIECE_WIDTH]);

  // Show the attack numbers
  if(gShowAttackBoard)
  {
    char val[4];
    char rop = blackWhite ? ROP_INV : ROP_CPY;

    sprintf(val,"%02X%d",gChessBoard[y][x],(gChessBoard[y][x]&PIECE_WHITE)>>7);

    plat_DrawChar(BOARD_X+x*BOARD_PIECE_WIDTH,BOARD_Y+(y+1)*BOARD_PIECE_HEIGHT-CHAR_HEIGHT,
                  rop,(gpAttackBoard[giAttackBoardOffset[position][0]])+'0');
    plat_DrawChar(BOARD_X+(x+1)*BOARD_PIECE_WIDTH-1,BOARD_Y+(y+1)*BOARD_PIECE_HEIGHT-CHAR_HEIGHT,
                  rop,(gpAttackBoard[giAttackBoardOffset[position][1]])+'0');
    plat_DrawString(BOARD_X+x*BOARD_PIECE_WIDTH,BOARD_Y+y*BOARD_PIECE_HEIGHT,rop,val);
  }
}

/*-----------------------------------------------------------------------*/
void plat_ShowSideToGoLabel(char side)
{
  plat_DrawString(28,SCREEN_HEIGHT-CHAR_HEIGHT,
                  side?ROP_CPY:ROP_INV,gszSideLabel[side]);
}

/*-----------------------------------------------------------------------*/
void plat_Highlight(char position, char color, char cursor)
{
  char y = position / 8, x = position & 7;
  char sel = color == HCOLOR_ATTACK? 3:2;

  memset(moveBuf,0x20,6); moveBuf[6] = 0;
  moveBuf[0] = 'A' + (gTile[0] & 7);
  moveBuf[1] = '8' - (gTile[0] / 8);

  switch(color)
  {
    case HCOLOR_INVALID:  // 2
    case HCOLOR_EMPTY:    // 4
      hires_DrawClr(SCREEN_WIDTH-6-1,(LOG_WINDOW_HEIGHT+1)*CHAR_HEIGHT,1);
      moveBuf[2] = 0;
      break;
    case HCOLOR_ATTACK:   // 3
      hires_DrawClr(SCREEN_WIDTH-6-1,(LOG_WINDOW_HEIGHT+1)*CHAR_HEIGHT,2);
      moveBuf[2] = (gPiece[1] & PIECE_DATA) != NONE ? 'x' : '-';
      moveBuf[3] = 'A' + (gTile[1] & 7);
      moveBuf[4] = '8' - (gTile[1] / 8);
      break;
    case HCOLOR_VALID:    // 5
      hires_DrawClr(SCREEN_WIDTH-6-1,(LOG_WINDOW_HEIGHT+1)*CHAR_HEIGHT,2);
      moveBuf[2] = 0;
      break;
    case HCOLOR_SELECTED: // 6
      moveBuf[2] = (gPiece[1] & PIECE_DATA) != NONE ? 'x' : '-';
      moveBuf[3] = 'A' + (gTile[1] & 7);
      moveBuf[4] = '8' - (gTile[1] / 8);
      break;
  }

  plat_DrawString(SCREEN_WIDTH-6,(LOG_WINDOW_HEIGHT+1)*CHAR_HEIGHT,
                  color?ROP_CPY:ROP_INV,moveBuf);

  plat_DrawSquare(position);
  hires_Select(BOARD_X+x*BOARD_PIECE_WIDTH,BOARD_Y+y*BOARD_PIECE_HEIGHT,
                BOARD_PIECE_WIDTH,BOARD_PIECE_HEIGHT,ROP_CPY,
                &hires_Pieces[sel*7*BOARD_PIECE_HEIGHT*BOARD_PIECE_WIDTH]);

  unused(cursor); // silence compiler
}
/*-----------------------------------------------------------------------*/
void plat_ShowMessage(char *str, char color)
{
  plat_DrawString(26,0,ROP_CPY,str);
  unused(color); // silence compiler
}
/*-----------------------------------------------------------------------*/
void plat_ClearMessage(void)
{
  hires_Mask(26,0,7,CHAR_HEIGHT,ROP_CPY);
}
/*-----------------------------------------------------------------------*/
void plat_AddToLogWin(void)
{
  char y;

  for(y=0; y<=LOG_WINDOW_HEIGHT; ++y)
  {
    hires_Mask(SCREEN_WIDTH-6,y*CHAR_HEIGHT,
               6,CHAR_HEIGHT,ROP_CPY);

    if(undo_FindUndoLine(LOG_WINDOW_HEIGHT-y))
    {
      frontend_FormatLogString();
      // force 2 empty lines
      if(1<y)
      {
        plat_DrawString(SCREEN_WIDTH-6,y*CHAR_HEIGHT,
                        gColor[0]?ROP_CPY:ROP_INV,gLogStrBuffer);
      }
    }
  }
}
/*-----------------------------------------------------------------------*/
void plat_AddToLogWinTop(void)
{
  plat_AddToLogWin();
}

/*-----------------------------------------------------------------------*/
int plat_ReadKeys(char blocking)
{
  char key = 0;
  int keyMask = 0;

  if(blocking)
  {
    do key = getkey(); while(!key);
  }
  else
    key = getkey();

  switch(key)
  {
    case KEY_LEFT:
      keyMask |= INPUT_LEFT;
      break;

    case KEY_RIGHT:
      keyMask |= INPUT_RIGHT;
      break;

    case KEY_UP:
      keyMask |= INPUT_UP;
      break;

    case KEY_DOWN:
      keyMask |= INPUT_DOWN;
      break;

    case KEY_ESC:
      keyMask |= INPUT_BACKUP;
      break;

    case KEY_RETURN:
      keyMask |= INPUT_SELECT;
      break;

    case 'A':
    case 'a':       // Show Attackers
      keyMask |= INPUT_TOGGLE_A;
      break;

    case 'B':
    case 'b':       // Board attacks - Show all attacks
      keyMask |= INPUT_TOGGLE_B;
      break;

    case 'D':
    case 'd':       // Show Defenders
      keyMask |= INPUT_TOGGLE_D;
      break;

    case 'M':
    case 'm':
      keyMask |= INPUT_MENU;
      break;

    case 'R':
    case 'r':
      keyMask |= INPUT_REDO;
      break;

    case 'U':
    case 'u':
      keyMask |= INPUT_UNDO;
      break;
  }

  return keyMask;
}

void plat_Shutdown(void)
{
  quit();
}


static char* scrptr = (char*)(0xbb80+25*40);
static char inverse = 0x00;

static void clrmenu(void)
{
  memset((void*)(0xbb80+25*40), 0x20, 3*40);
  scrptr = (char*)(0xbb80+25*40);
}

static void cputs(char* s)
{
  while(s && *s)
    *scrptr++ = inverse | *s++;
}

static void gotoxy(int x, int y)
{
  scrptr = (char*)(0xbb80+(25+y)*40+x);
}

static void revers(char flag)
{
  inverse = flag? 0x80:0x00;
}

static void cprintf(char* fmt, char* s)
{
  static char temp[64];
  sprintf(temp, fmt, s);
  cputs(temp);
}
