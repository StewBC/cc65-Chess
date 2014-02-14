/*
 *	board.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "board.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
void board_LoadMoves(char x, char y, char dx, char dy, char n, char addDefenceMove);
void board_GenPawnMoves(char position, char color, char addDefenceMove);
void board_GenRookMoves(char position, char addDefenceMove);
void board_GenKnightMoves(char position, char addDefenceMove);
void board_GenBishopMoves(char position, char addDefenceMove);
void board_GenKingMoves(char position, char addDefenceMove);
void board_GenQueenMoves(char position, char addDefenceMove);
char board_CheckLineAttack(char t1, char t2, char side);
void board_UpdateAttackGrid(int offset, char side);

/*-----------------------------------------------------------------------*/
// board_UpdateAttackGrid may add entries (behind the king in a line 
// attack, for example) and these variables track those changes so they
// can easily be undone
static int	si_fixupTable[(8*2)+7+6];
static char sc_numFixes;

/*-----------------------------------------------------------------------*/
void board_Init()
{
	char i;
	
	memset(gpChessBoard,NONE,8*8*sizeof(char));
				
	gChessBoard[0][0] = gChessBoard[7][0] = ROOK;
	gChessBoard[0][1] = gChessBoard[7][1] = KNIGHT;
	gChessBoard[0][2] = gChessBoard[7][2] = BISHOP;
	gChessBoard[0][3] = gChessBoard[7][3] = QUEEN;
	gChessBoard[0][4] = gChessBoard[7][4] = KING;
	gChessBoard[0][5] = gChessBoard[7][5] = BISHOP;
	gChessBoard[0][6] = gChessBoard[7][6] = KNIGHT;
	gChessBoard[0][7] = gChessBoard[7][7] = ROOK;

	for(i=0; i < 8; ++i)
	{
		gChessBoard[1][i] = PAWN;
		gChessBoard[6][i] = PAWN | PIECE_WHITE;
		gChessBoard[7][i] |= PIECE_WHITE;
	}
	
	gKingData[0] = MK_POS(0,4);
	gKingData[1] = MK_POS(7,4);

	// Not part of the board but does need resetting for every game
	gCursorPos[SIDE_BLACK][1] = gCursorPos[SIDE_WHITE][1] = 4;
	gCursorPos[SIDE_BLACK][0] = 0;
	gCursorPos[SIDE_WHITE][0] = 7;
	
	board_PlacePieceAttacks();
}

/*-----------------------------------------------------------------------*/
// For every piece on the board, make entries in the attack db of what it
// is attacking or defending
void board_PlacePieceAttacks()
{
	char i, j, piece;

	memset(gpAttackBoard,0,8*8*ATTACK_WIDTH);

	for(i=0; i<64; ++i)
	{
		piece = gpChessBoard[i];
		if(piece != NONE)
		{
			char color = (piece & PIECE_WHITE) >> 7;
			board_GeneratePossibleMoves(i, 1);
			if(gNumMoves)
			{
				for(j=0; j<gNumMoves; ++j)
				{
					int offset = giAttackBoardOffset[gPossibleMoves[j]][color];
					char attackers = gpAttackBoard[offset];
					gpAttackBoard[offset+1+attackers] = i;
					++gpAttackBoard[offset];
				}
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
// For straight-moving pieces, add open tiles or tiles that meet the
// inclusion criteria to the gPossibleMoves buffer
void board_LoadMoves(char x, char y, char dx, char dy, char n, char addDefenceMove)
{
	char piece, /*color,*/ my_color = gChessBoard[y][x] & PIECE_WHITE;

	do
	{
		x += dx;
		y += dy;
		--n;
		// Don't go over the edge of the board.	 
		// Unsigned char will wrap at -1 to 255
		if(x > 7 || y > 7)
			break;

		piece = gChessBoard[y][x];
		// color = piece & PIECE_WHITE;
		// piece &= PIECE_DATA;

		if(NONE == piece || addDefenceMove || my_color != (piece & PIECE_WHITE))
			gPossibleMoves[gNumMoves++] = MK_POS(y,x);

		// any non-emty tiles ends the scan
		if(NONE != piece)
			break;
			
	} while(n);
}

/*-----------------------------------------------------------------------*/
// position is a tile number 0..63.	 If there's a piece on the tile,
// it's available moves will be placed in gPossibleMoves.  if 
// addDefenceMove is non-zero, moves onto friendly pieces and moves
// by PAWNS to capture are allso added (defence and pawn-take)
void board_GeneratePossibleMoves(char position, char addDefenceMove)
{
	char piece, color;

	// Reset the global counter - used as an index into the
	// global buffer for tracking moves for a piece
	gNumMoves = 0;

	piece = gpChessBoard[position];
	color = (piece & PIECE_WHITE) >> 7;
	piece &= PIECE_DATA;

	// Call the piece-specific function to generate the list
	// for the piece at tile "position"
	switch(piece)
	{
		case PAWN:
		board_GenPawnMoves(position, color, addDefenceMove);
		break;
		
		case ROOK:
		board_GenRookMoves(position, addDefenceMove);
		break;

		case KNIGHT:
		board_GenKnightMoves(position, addDefenceMove);
		break;

		case BISHOP:
		board_GenBishopMoves(position, addDefenceMove);
		break;

		case KING:
		board_GenKingMoves(position, addDefenceMove);
		break;

		case QUEEN:
		board_GenQueenMoves(position, addDefenceMove);
		break;
	}
}

/*-----------------------------------------------------------------------*/
void board_GenPawnMoves(char position, char sideColor, char addDefenceMove)
{
	const signed char cap_x[2] = {-1, 1};
	char i, nx, piece, color, y = position / 8, x = position & 7;
	signed char d = sideColor ? -1 : 1;
	
	if(y == 0 || y == 7)
		return;
	
	// if addDefenceMove is true, the move forward of a pawn isn't added
	// becase a pawn doesn't "defend" the next square in that it can't
	// take the piece there
	if(!addDefenceMove && NONE == gChessBoard[y+d][x])
	{
		gPossibleMoves[gNumMoves++] = MK_POS(y+d,x);
		if(((sideColor && y == 6) || (!sideColor && y == 1)) && NONE == gChessBoard[y+(2*d)][x])
			gPossibleMoves[gNumMoves++] = MK_POS(y+(2*d),x);
	}
	
	for(i=0; i<2; ++i)
	{
		nx = x + cap_x[i];
		if(nx > 7)
			continue;

		position = MK_POS(y+d,nx);
		
		if(gEPPawn != position)
		{
			// Read the piece from the chessboard
			piece = gChessBoard[y+d][nx];
			color = (piece & PIECE_WHITE) >> 7;
			piece &= PIECE_DATA;
		}
		else
		{
			// Create a fake piece to represent the en passant pawn
			piece = PAWN;
			color = 1 - sideColor;
		}
	
		if(addDefenceMove || (NONE != piece && color != sideColor))
			gPossibleMoves[gNumMoves++] = position;
	}
}

/*-----------------------------------------------------------------------*/
void board_GenRookMoves(char position, char addDefenceMove)
{
	char y = position / 8, x = position & 7;

	board_LoadMoves(x, y, -1,  0, 8, addDefenceMove);
	board_LoadMoves(x, y,  0, -1, 8, addDefenceMove);
	board_LoadMoves(x, y,  1,  0, 8, addDefenceMove);
	board_LoadMoves(x, y,  0,  1, 8, addDefenceMove);
}

/*-----------------------------------------------------------------------*/
void board_GenKnightMoves(char position, char addDefenceMove)
{
	char y = position / 8, x = position & 7;
	
	board_LoadMoves(x, y, -2, -1, 1, addDefenceMove);
	board_LoadMoves(x, y, -1, -2, 1, addDefenceMove);
	board_LoadMoves(x, y,  1, -2, 1, addDefenceMove);
	board_LoadMoves(x, y,  2, -1, 1, addDefenceMove);
	board_LoadMoves(x, y,  2,  1, 1, addDefenceMove);
	board_LoadMoves(x, y,  1,  2, 1, addDefenceMove);
	board_LoadMoves(x, y, -1,  2, 1, addDefenceMove);
	board_LoadMoves(x, y, -2,  1, 1, addDefenceMove);
}

/*-----------------------------------------------------------------------*/
void board_GenBishopMoves(char position, char addDefenceMove)
{
	char y = position / 8, x = position & 7;
	
	board_LoadMoves(x, y, -1, -1, 8, addDefenceMove);
	board_LoadMoves(x, y,  1, -1, 8, addDefenceMove);
	board_LoadMoves(x, y,  1,  1, 8, addDefenceMove);
	board_LoadMoves(x, y, -1,  1, 8, addDefenceMove);
}

/*-----------------------------------------------------------------------*/
void board_GenKingMoves(char position, char addDefenceMove)
{
	char okayToCastle[2] = {0,0}, y = position / 8, x = position & 7;

	// If the king hasn't moved (| PIECE_MOVED == 0)
	if(KING == (gpChessBoard[position] & (PIECE_DATA | PIECE_MOVED)))
	{
		// and the rook hasn't moved
		if(ROOK == (gpChessBoard[position-4] & (PIECE_DATA | PIECE_MOVED)))
		{
			// and there are 3 open spaces between them, then
			// casteling is possible
			board_LoadMoves(x, y, -1,  0, 3, addDefenceMove);
			if(3 == gNumMoves)
				okayToCastle[0] = 1;
			gNumMoves = 0;
		}
		
		if(ROOK == (gpChessBoard[position+3] & (PIECE_DATA | PIECE_MOVED)))
		{
			// on this side, 2 open spaces is all that's needed
			board_LoadMoves(x, y,  1,  0, 2, addDefenceMove);
			if(2 == gNumMoves)
				okayToCastle[1] = 1;
			gNumMoves = 0;
		}
	}

	if(okayToCastle[0])
		board_LoadMoves(x, y, -2,  0, 1, addDefenceMove);
		
	if(okayToCastle[1])
		board_LoadMoves(x, y,  2,  0, 1, addDefenceMove);
		
	board_LoadMoves(x, y, -1,  0, 1, addDefenceMove);
	board_LoadMoves(x, y, -1, -1, 1, addDefenceMove);
	board_LoadMoves(x, y,  0, -1, 1, addDefenceMove);
	board_LoadMoves(x, y,  1, -1, 1, addDefenceMove);
	board_LoadMoves(x, y,  1,  0, 1, addDefenceMove);
	board_LoadMoves(x, y,  1,  1, 1, addDefenceMove);
	board_LoadMoves(x, y,  0,  1, 1, addDefenceMove);
	board_LoadMoves(x, y, -1,  1, 1, addDefenceMove);
}

/*-----------------------------------------------------------------------*/
void board_GenQueenMoves(char position, char addDefenceMove)
{
	char y = position / 8, x = position & 7;
	
	board_LoadMoves(x, y, -1,  0, 8, addDefenceMove);
	board_LoadMoves(x, y, -1, -1, 8, addDefenceMove);
	board_LoadMoves(x, y,  0, -1, 8, addDefenceMove);
	board_LoadMoves(x, y,  1, -1, 8, addDefenceMove);
	board_LoadMoves(x, y,  1,  0, 8, addDefenceMove);
	board_LoadMoves(x, y,  1,  1, 8, addDefenceMove);
	board_LoadMoves(x, y,  0,  1, 8, addDefenceMove);
	board_LoadMoves(x, y, -1,  1, 8, addDefenceMove);
}

/*-----------------------------------------------------------------------*/
// This is the function that will move pieces from gTile[0] to gTile[1]
// if the move is valid.  It will make sure pieces dont' do illegal moves
char board_ProcessAction(void)
{
	// Get the king's tile and an offset to the king's attackers
	char kingTile = gKingData[gColor[0]];

	// If the king is moving onto a tile under attack the move is invalid
	// Can't move into check
	if(KING == gPiece[0] && gpAttackBoard[giAttackBoardOffset[gTile[1]][1-gColor[0]]])
		return OUTCOME_INVALID;

	if(KING == gPiece[0])
	{
		kingTile = gTile[1];
		// See if the King is trying to castle
		board_ProcessCastling(3, 2);
	}
	else if(PAWN == gPiece[0])
	{
		if(gEPPawn == gTile[1])
			board_ProcessEnPassant(ENPASSANT_TAKE);
		else if(gTile[1] < 8 || gTile[1] > 55)
		{
			if(!gAI)
				gpChessBoard[gTile[0]] = frontend_GetPromotion() | (gpChessBoard[gTile[0]] & PIECE_EXTRA_DATA); 
			else
				gpChessBoard[gTile[0]] = QUEEN | (gpChessBoard[gTile[0]] & PIECE_EXTRA_DATA);	
		}
	}

	// Make the move effective
	gpChessBoard[gTile[1]] = gpChessBoard[gTile[0]];
	gpChessBoard[gTile[0]] = NONE;
	
	// Recalculate the attack DB for the post-move board
	board_PlacePieceAttacks();

	// If the king is (still) under attack, the move isn't valid.  Can't move/leave king in check
	if(gpAttackBoard[giAttackBoardOffset[kingTile][1-gColor[0]]])
	{
		// restore the board, incl. reversing a casteling move or unpromoting a pawn, etc.
		gpChessBoard[gTile[0]] = gpChessBoard[gTile[1]];
		gpChessBoard[gTile[1]] = gPiece[1] | (gColor[1] << 7) | gMove[1];
		if(PAWN == gPiece[0])
		{
			if(gEPPawn == gTile[1])
			{
				gpChessBoard[gTile[2]] = PAWN | ((1-gColor[0]) ? PIECE_WHITE : 0);
				gTile[2] = NULL_TILE;
			}
			else if(gTile[1] < 8 || gTile[1] > 55)
				gpChessBoard[gTile[0]] = PAWN | (gpChessBoard[gTile[0]] & PIECE_EXTRA_DATA);
		}
		else if(KING == gPiece[0])
			board_ProcessCastling(2, 3);
		
		// Set the attack DB back to how it was before the move was put into effect
		board_PlacePieceAttacks();
		// Regenerate the possible moves for the selected piece as it was destroyed by PlacePieceAttacks
		board_GeneratePossibleMoves(gTile[0], 0);
		
		return OUTCOME_INVALID;
	}

	// Reset the en passant decoy at every move
	gEPPawn = NULL_TILE;
	
	// Update the special king-tile-tracker
	if(PAWN == gPiece[0])
	{
		if(!gMove[0])
			board_ProcessEnPassant(ENPASSANT_MAYBE);
	}
	else if(KING == gPiece[0])
		gKingData[gColor[0]] = gTile[1];
	
	// The move succeeded so mark the piece as having moved
	gpChessBoard[gTile[1]] |= PIECE_MOVED;
	
	// If the opposing king is in check
	if(gpAttackBoard[giAttackBoardOffset[gKingData[1-gColor[0]]][gColor[0]]])
	{
		sc_numFixes = 0;
		// Check if that king is in check-mate
		return board_CheckForMate(1-gColor[0]);
	}
	
	return OUTCOME_OK;
}

/*-----------------------------------------------------------------------*/
// Use Bresenham's algorithm to see what tiles lie
// between the attacker and the king
// not for use with a knight attacker
char board_CheckLineAttack(char t1, char t2, char side)
{
	int offset;
	signed char y1, x1, y2, x2, dx, dy, sx, sy, err, e2;
	char piece, defenders;

	y1 = t1 / 8;
	x1 = t1 & 7;
	y2 = t2 / 8;
	x2 = t2 & 7;

	dx = abs(x2-x1);
	dy = abs(y2-y1);

	if(x1 < x2)
		sx = 1; 
	else 
		sx = -1;

	if(y1 < y2)
		sy = 1;
	else
		sy = -1;

	err = dx - dy;

	// for every block from the attacker to the king, consider
	while(1)
	{
		if(x1 == x2 && y1 == y2)
			break;

		offset = giAttackBoardOffset[t1][side];

		// How many of the king's pieces attack this block, and what is on the block
		defenders = gpAttackBoard[offset];
		piece = gpChessBoard[t1] & PIECE_DATA;

		if(defenders)
		{
			char i;

			// For every defender of the king, see if they can break the check-mate
			for(i=1; i<=defenders; ++i)
			{
				// Who is defending and what tile is the defender on
				char defTile = gpAttackBoard[offset+i];
				char defPiece = gpChessBoard[defTile] & PIECE_DATA;
				
				if(PAWN == defPiece)
				{
					// see if the pawn is in a move or attack position relative to the 
					// path of the attacker
					char d1 = (defTile+8), d2 = (defTile-8);
					
					// If it's the path, not the attacker self
					if(NONE == piece)
					{
						// if the pawn can move onto the path then it's not check-mate
						if(d1 == t1 || d2 == t1)
							return OUTCOME_CHECK;
					}
					else
					{
						// if it's the attacker itself and the pawn can take it, then
						// it's not check-mate
						if(d1 != t1 && d2 != t1)
							return OUTCOME_CHECK;
					}
				}
				// if the defender is the king and the code is here, it means
				// the attacker has backup (or the code won't get here) and the 
				// king can't take the attacker (because he would still be under attack).
				// For any other defender, the defender can take the attacker and 
				// it's not check-mate
				else if(KING != defPiece)
					return OUTCOME_CHECK;
			}
		}
		
		e2 = err << 1;
		if(e2 > -dy) 
		{
			err = err - dy;
			x1 = x1 + sx;
		}
		if(e2 <	 dx)
		{
			err = err + dx;
			y1 = y1 + sy;
		}
		t1 = MK_POS(y1, x1);
	}
	
	return OUTCOME_CHECKMATE;
}	

/*-----------------------------------------------------------------------*/
// Make sure gAttackBoard has all the moves in it that's needed to determine a check-mate
// Offset is in the attack DB for the attackers of the king on "side"
void board_UpdateAttackGrid(int offset, char side)
{
	char i, j, piece, color, tile, king, attackers, numAttackers, *attackPieces;
	
	// Get a handle to the pieces that are attacking the king, causing check
	numAttackers = gpAttackBoard[offset];
	attackPieces = &gpAttackBoard[offset+1];

	// Backup the king and remove him from the board
	tile = gKingData[side];
	king = gpChessBoard[tile];
	gpChessBoard[tile] = NONE;

	for(i=0; i<64; ++i)
	{
		piece = gpChessBoard[i];
		if(piece != NONE)
		{
			color = (piece & PIECE_WHITE) >> 7;
			piece &= PIECE_DATA;

			// if the piece is one of the kings' pawns, now a move forward
			// is also an "attack" (or a defensive move) that can cut the
			// path from the attacker to the king, so add these moves
			if(PAWN == piece && color == side)
			{
				signed char di, d = side ? -8 : 8;
				
				di = i+d;
				if(di < 0 || di > 63)
					continue;
				
				// Check the square in front of the pawn
				if(NONE == gpChessBoard[di])
				{
					offset = giAttackBoardOffset[di][color];
					attackers = gpAttackBoard[offset] + 1;
					gpAttackBoard[offset+attackers] = i;
					// Keep a log of all modifications so this can be "unwound" at the end
					// to return gAttackBoard to the in-game state without re-generating from
					// scratch
					++gpAttackBoard[offset];
					si_fixupTable[sc_numFixes++] = offset;

					// and also 2 in front of, if the pawn hasn't moved yet
					// if(((side && (i >= 48 && i <= 55)) || (!side && (i >= 8 && i <= 15))) && NONE == gpChessBoard[i+(2*d)])
					if(!(gpChessBoard[i] & PIECE_MOVED) && NONE == gpChessBoard[i+(2*d)])
					{
						offset += (d*ATTACK_WIDTH);
						attackers = gpAttackBoard[offset] + 1;
						gpAttackBoard[offset+attackers] = i;
						++gpAttackBoard[offset];
						si_fixupTable[sc_numFixes++] = offset;
					}
				}
			}
			// If the piece is an enemy piece, and not a knight
			else if(color != side && piece != KNIGHT)
			{
				// Is the piece directly attacking the king
				if(board_findInList(attackPieces, numAttackers, i))
				{
					// Get a list of all blocks this piece is attacking -
					// this list will include blocks "behind" the king, since
					// the king has been removed
					board_GeneratePossibleMoves(i, 1);
					j = 0;
					while(j < gNumMoves)
					{
						offset = giAttackBoardOffset[gPossibleMoves[j++]][color];
						attackers = gpAttackBoard[offset];
						// See if this tile has already been marked as being attacked by this piece
						if(!board_findInList(&gpAttackBoard[offset+1], attackers, i))
						{
							// If not, mark it as being under attack from this piece -
							// these are the tiles "behind" the king
							si_fixupTable[sc_numFixes++] = offset;
							++gpAttackBoard[offset++];
							gpAttackBoard[offset+attackers] = i;
						}
					}
				}
			}
		}
	}

	// Put the king back on the board
	gpChessBoard[tile] = king;
}

/*-----------------------------------------------------------------------*/
void board_ProcessEnPassant(char state)
{
	switch(state)
	{
		case ENPASSANT_TAKE:	// Take the pawn
			if((gTile[0] & 7) < (gTile[1] & 7))
				gTile[2] = gTile[0] + 1;
			else
				gTile[2] = gTile[0] - 1;

			gpChessBoard[gTile[2]] = NONE;
		break;

		case ENPASSANT_UNTAKE:	// Reverse 1; Restore the pawn
			if((gTile[0] & 7) < (gTile[1] & 7))
				gTile[2] = gTile[0] + 1;
			else
				gTile[2] = gTile[0] - 1;
			
			gpChessBoard[gTile[2]] = PAWN | ((1-gColor[0]) ? PIECE_WHITE : 0);
			gEPPawn = gTile[1];
		break;
			
		case ENPASSANT_MAYBE:	// See if the move forwards creates an en passant opportunity
		{
			char x0 = (gTile[0] / 8), x1 = (gTile[1] / 8);
		
			if(x0 < x1)
			{
				if(x0 == x1 - 2)
					gEPPawn = gTile[0] + 8;
			}
			else
			{
				if(x0 == x1 + 2)
					gEPPawn = gTile[0] - 8;
			}
		}
		break;
	}
}

/*-----------------------------------------------------------------------*/
// This moves the rook and marks its move bit if the king is moving
// to cause a "casteling" move.
// Can be called with the parameters reversed to undo the casteling, 
// whcih also clears the move bit on the rook.
void board_ProcessCastling(char a, char b)
{
	gTile[2] = gTile[3] = NULL_TILE;
	
	// If the king hasn't moved and is now moving
	// 2 spaces, it's already known that the rook hasn't moved
	// because that's the only way this shows up as
	// a valid move, so no need to check the rook.	The reason
	// to check the king is to make sure this is the
	// first move for the king, otherwise this code should
	// not do anything.
	if(gTile[1] == gKingMovingTo[gColor[0]][0] && !gMove[0])
	{
		gTile[2] = gTile[1] - 2;
		gTile[3] = gTile[1] + 1;
	}
	else if(gTile[1] == gKingMovingTo[gColor[0]][1] && !gMove[0])
	{
		gTile[2] = gTile[1] + 1;
		gTile[3] = gTile[1] - 1;
	}
	
	if(NULL_TILE != gTile[2])
	{
		gpChessBoard[gTile[a]] = gpChessBoard[gTile[b]];
		if(a > b)
			gpChessBoard[gTile[a]] |= PIECE_MOVED;
		else
			gpChessBoard[gTile[a]] &= ~PIECE_MOVED;
			
		gpChessBoard[gTile[b]] = NONE;
	}
}

/*-----------------------------------------------------------------------*/
// This is called to see if the king on "side" is in check-mate
char board_CheckForMate(char side)
{
	int offset;
	char i, tile, other = 1-side;

	// Look at the king's attackers
	tile = gKingData[side];
	offset = giAttackBoardOffset[tile][other];

	// Make gAttackBoard contain all needed attacks and defences
	// to determine a check mate state
	board_UpdateAttackGrid(offset, side);
	
	// See where the king might hide
	board_GeneratePossibleMoves(tile, 0);

	// This also triggers if the attacker has no backup and the 
	// king can take it
	for(i=0; i<gNumMoves; ++i)
	{
		offset = giAttackBoardOffset[gPossibleMoves[i]][other];
		if(!gpAttackBoard[offset])
			return OUTCOME_CHECK;
	}

	// Go back to the attackers of the king
	offset = giAttackBoardOffset[tile][other];
	
	// If there's more than one attacker then it's mate
	if(gpAttackBoard[offset] > 1)
		return OUTCOME_CHECKMATE;

	// There's only 1 attacker, so work with it
	tile = gpAttackBoard[offset+1];
	
	// Deal with a knight attacker here as there is no "interruptable path"
	// between a horse attacker and the king
	if(KNIGHT == (gpChessBoard[tile] & PIECE_DATA))
	{
		// Get an offset to the defenders ("attackers" on same side as king)
		offset = giAttackBoardOffset[tile][side];

		// See if the Horse can be taken by any defenders
		if(gpAttackBoard[offset])
			return OUTCOME_CHECK;

		// If not then it's check-mate
		return OUTCOME_CHECKMATE;
	}

	// See if the attacker can be eliminated or the path between the attacker
	// and the king can be blocked
	i = board_CheckLineAttack(tile, gKingData[side], side);
	
	// Clean up changes it made to the attack DB in board_UpdateAttackGrid
	while(sc_numFixes)
	{
		--sc_numFixes;
		--gpAttackBoard[si_fixupTable[sc_numFixes]];
	}

	return i;
}

/*-----------------------------------------------------------------------*/
// Utility function to find a number in a series of numbers
char board_findInList(char *list, char numElements, char number)
{
	while(numElements--)
	{
		if(number == list[numElements])
			return 1;
	}
	return 0;
}
