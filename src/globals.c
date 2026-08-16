/*
 *	globals.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include "types.h"
#include "globals.h"

/*-----------------------------------------------------------------------*/
char		gChessBoard[8][8];							// Display mirror of the engine board
char		gpAttackBoard[64*2];						// Attacker counts, [tile][side]
char		giAttackBoardOffset[64][2];					// Index into the counts above

char		gUserMode;									// =0, all AI; =1, Black is user; =2, White; =3, both
char		gMoveCounter;								// Moves without a piece taken for 50 move rule
char		gTile[2];									// [0] = src tile, [1] = dst, as 0..63 for the log
char		gPiece[2];									// [0] = piece moved, [1] = piece taken
char		gColor[2];									// [0] = color of the piece that moved
char		gOutcome;									// Result of the last move, for the log
char		gSkillLevel;								// 0..3, indexes gcSearchSkill
char		gReturnToOS;								// =1 can quit game; =0 cannot quit game
char		gCursorPos[2][2];							// Remember last cursor pos for human players
char		gGotoTile = 255;							// 255 = none; Mac mouse jumps the cursor with this
char		gShowAttackBoard;							// Visibility toggle
char		gShowAttacks[2];							// Visibility toggle per side
char		gLogStrBuffer[7];							// String placeholder for the move log

/*-----------------------------------------------------------------------*/
// All Display Strings
char		gMoveSymbol[OUTCOME_STALEMATE] = {'\0', '+', '#', '/', '!'};
char		gszNoUndo[] = "No Undo";
char		gszNoRedo[] = "No Redo";
char		gszInvalid[] = "Invalid";
char		gszThinking[] = "Think";
char		gszAbout[] = "cc65 Chess V2.0 by S. Wessels, 2026.    ";
char		gszResume[] = "Resume Game    ";
char		gszQuit[] = "Quit Game      ";
char		gszSelect[] = "    Select     ";
char		gszpromote[] = "Select a rank to promote the pawn to. ";
char*		gMainMenu[] = {gszSelect, "1 Human player ","2 Human players","Both players AI",gszQuit, 0, 0};
char*		gSkillMenu[] = {gszSelect, "  Very Easy    ","  Easy         ","  Harder       ","  Very Hard    ", 0};
char*		gColorMenu[] = {gszSelect,"  Play White   ","  Play Black   ", 0};
char*		gAreYouSureMenu[] = {" Are you sure? ","  Absolutely!  ","  Not so much  ",0};
char*		gPromoteMenu[] = {"Promotion", "  Queen  ", "  Rook   ", "  Bishop ", "  Knight ", 0};
char*		gszSideLabel[2] = {"Black", "White"};
