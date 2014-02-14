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
char		gChessBoard[8][8];							// The chess board itself
char*		gpChessBoard = &gChessBoard[0][0];			// With a linear address alias

// This is an 8x8 grid with a 2-part array for each cell.  At [8][8][0] is the
// number of entries to follow between [y][x]][0] and [y][x][17].  At [y][x][17]
// is the number of entries between [y][x][17] and [y][x+1][0].  These between
// entries are tiles of black [0] or white [17] that can land on this tile to
// take (or defend) whatever piece is on the tile.  I refer to this as the
// Attack DB on the code
char		gAttackBoard[8][8][ATTACK_WIDTH];			
char*		gpAttackBoard = &gAttackBoard[0][0][0];		// Linear version
int			giAttackBoardOffset[64][2];					// Offsets to the [0]'s in the AttackDB
char		gUserMode;									// =0, all AI; =1, Black is user; =2, White; =3, both
char		gAI;										// Prevents AI code from asking for pawn promotion
char		gMoveCounter;								// Moves without a piece taken for 50 move rule
char		gTile[4];									// [0] = src tile, 1 = to dst
char		gPiece[2];									// [0] = piece at src, [1] = piece at dst
char		gMove[2];									// [0] = piece0 moved or no, ditto [1] on piece1
char		gColor[2];									// [0] =1 white piece0, =0 black; [1] ditto on piece1
char		gOutcome;									// Result of board_ProcessAction for log. Bad design
char		gEPPawn;									// Tile where a pawn can be taken en passant or NULL_TILE
char		gMaxLevel;									// How many levels down the AI thinks ahead. 0 = self and opponent
char		gWidth;										// How many of the top ranked moves for self tried on think ahead
char		gDeepThoughts;								// =1 - moves are check and AttackDB updated; =0 - inaccurate but faster
char		gReturnToOS;								// =1 can quit game; =0 cannot quit game
char		gPossibleMoves[MAX_PIECE_MOVES];			// All the tiles a piece can get to from current pos
char		gNumMoves;									// Entries in gPossibleMoves
char		gCursorPos[2][2];							// Remember last cursor pos for human players
char		gKingData[2];								// Tracks the king's tile on the board
char		gShowAttackBoard;							// Visibility toggle
char		gShowAttacks[2];							// Visibility toggle per side
char		gLogStrBuffer[7];							// String placeholder for the movve log
char		gKingMovingTo[2][2] = {{2,6},{58,62}};		// Tiles where a king lands when casteling
char		gSkill[4*3] = {1,0,0,16,1,0,16,2,1,16,3,1}; // Values for gWidth, gMaxLevel & gDeepThoughts over 4 skills

// Values AI sees when looking at pieces; +2 compensates for other move hints and 3 * makes the
// piece value much more meaningful
char		gPieceValues[PAWN+1] = {					
		(0),	// NONE
	2+(3*5),	// ROOK,
	2+(3*3),	// KNIGHT,
	2+(3*3),	// BISHOP,
	2+(3*10),	// KING,
	2+(3*9),	// QUEEN,
	2+(3*1),	// PAWN,
};

/*-----------------------------------------------------------------------*/
// All Display Strings
char		gMoveSymbol[OUTCOME_STALEMATE] = {'\0', '+', '#', '/', '!'};
char		gszNoUndo[] = "No Undo";
char		gszNoRedo[] = "No Redo";
char		gszInvalid[] = "Invalid";
char		gszAbout[] = "cc65 Chess V1.0 by S. Wessels, 2014.    ";
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
