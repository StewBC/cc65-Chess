/*
 *	globals.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 *	The engine's real state lives in engine.h now (geBoard and friends).  What
 *	is left here is the game and display state, plus a small compatibility
 *	view that the platform files read directly:
 *
 *	  gChessBoard                              - pieces, for drawing
 *	  gpAttackBoard[giAttackBoardOffset[t][s]] - attacker counts, for the B display
 *	  gTile, gPiece, gColor                    - the move log line
 *
 *	Those five names and their meanings are frozen.  Every port reads them, and
 *	most of the ports cannot be built or tested here, so they do not change.
 */

#ifndef _GLOBALS_H_
#define _GLOBALS_H_

/*-----------------------------------------------------------------------*/
// Display mirror of the engine board, in the old 0..63 tile order with
// gChessBoard[0][0] = a8.  board_SyncDisplay refreshes it
extern char		gChessBoard[8][8];

// Attacker counts per tile per side, not the old attack database.  The ports
// only ever read the count, so that is all this holds now - 128 bytes where
// the database was 2176
extern char		gpAttackBoard[64*2];
extern char		giAttackBoardOffset[64][2];

/*-----------------------------------------------------------------------*/
extern char		gUserMode;									// =0, all AI; =1, Black is user; =2, White; =3, both
extern char		gMoveCounter;								// Moves without a piece taken for 50 move rule
extern char		gTile[2];									// [0] = src tile, [1] = dst, as 0..63 for the log
extern char		gPiece[2];									// [0] = piece moved, [1] = piece taken
extern char		gColor[2];									// [0] = color of the piece that moved
extern char		gOutcome;									// Result of the last move, for the log
extern char		gSkillLevel;								// 0..3, indexes gcSearchSkill
extern char		gReturnToOS;								// =1 can quit game; =0 cannot quit game
extern char		gCursorPos[2][2];							// Remember last cursor pos for human players
extern char		gShowAttackBoard;							// Visibility toggle
extern char		gShowAttacks[2];							// Visibility toggle per side
extern char		gLogStrBuffer[7];							// String placeholder for the move log

/*-----------------------------------------------------------------------*/
// Display labels
extern char		gMoveSymbol[OUTCOME_STALEMATE];
extern char		gszNoUndo[];
extern char		gszNoRedo[];
extern char		gszInvalid[];
extern char		gszThinking[];
extern char		gszAbout[];
extern char		gszResume[];
extern char		gszQuit[];
extern char		gszpromote[];
extern char*	gMainMenu[];
extern char*	gSkillMenu[];
extern char*	gColorMenu[];
extern char*	gAreYouSureMenu[];
extern char*	gPromoteMenu[];
extern char*	gszSideLabel[2];

#endif //_GLOBALS_H_
