/*
 *	globals.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _GLOBALS_H_
#define _GLOBALS_H_

extern char		gChessBoard[8][8];
extern char*	gpChessBoard;
extern char		gAttackBoard[8][8][ATTACK_WIDTH];
extern char*	gpAttackBoard;
extern int		giAttackBoardOffset[64][2];
extern char		gUserMode;
extern char		gAI;
extern char		gMoveCounter;
extern char		gTile[4];
extern char		gPiece[2];
extern char		gMove[2];
extern char		gColor[2];
extern char		gOutcome;
extern char		gEPPawn;
extern char		gMaxLevel;
extern char		gWidth;
extern char		gDeepThoughts;
extern char		gReturnToOS;
extern char		gPossibleMoves[MAX_PIECE_MOVES];
extern char		gNumMoves;
extern char		gCursorPos[2][2];
extern char		gKingData[2];
extern char		gShowAttackBoard;
extern char		gShowAttacks[2];
extern char		gLogStrBuffer[7];
extern char		gKingMovingTo[2][2];
extern char		gSkill[4*3];
extern char		gPieceValues[PAWN+1];

/*-----------------------------------------------------------------------*/
// Display labels
extern char		gMoveSymbol[OUTCOME_STALEMATE];
extern char		gszNoUndo[];
extern char		gszNoRedo[];
extern char		gszInvalid[];
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