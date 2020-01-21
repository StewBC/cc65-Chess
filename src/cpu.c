/*
 *	cpu.c
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#include <limits.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "undo.h"
#include "board.h"
#include "cpu.h"
#include "frontend.h"
#include "plat.h"

/*-----------------------------------------------------------------------*/
// Used to keep a sorted array of scores with source-destination tiles
typedef struct tagScore
{
	char		m_piece;
	char		m_source;
	char		m_dest;
	int			m_score;
} t_score;

/*-----------------------------------------------------------------------*/
// Internal function Prototype
void cpu_AddScoreSorted(t_score array[], char width, char source, char dest, int score);
void cpu_InitPieceData(char side);
void cpu_HolisticScore(char side);
int cpu_SourceScore(char position);
int cpu_DestScore(char position, char destination);
void cpu_ScorePieceMoves(void);
int cpu_FindBestOpponentMove(char side, char *from, char *to);
int cpu_ScorePieceSubTree(char level, char side, signed char sign, char src, char dst);

/*-----------------------------------------------------------------------*/
static const signed char scsc_neighbours[] = {-9,-8,-7,-1,1,7,8,9};
static t_score sts_pieceScores[NUM_PIECES_SIDE], sts_moveScore[MAX_PIECE_MOVES];
static char sc_index[64], sc_myMoves[MAX_PIECE_MOVES], sc_numMyMoves;

/*-----------------------------------------------------------------------*/
// Insert scores into an array making sure valid destinations end up
// at the head (index[0]), sorted by score
void cpu_AddScoreSorted(t_score array[], char width, char source, char dest, int score)
{
	char i;
	
	if(NULL_TILE == dest)
		return;
	
	for(i=0; i<width; ++i)
	{
		if(array[i].m_dest == NULL_TILE || score >= array[i].m_score)
		{
			memmove(&array[i+1], &array[i], ((width-1)-i)*sizeof(t_score));
			array[i].m_source = source;
			array[i].m_dest = dest;
			array[i].m_score = score;
			break;
		}
	}
}

/*-----------------------------------------------------------------------*/
// Init two helper data structures
void cpu_InitPieceData(char side)
{
	char i, piece, iPiece = 0;

	// Init the twho helper arrays.  The sc_index is easily removed
	// but having it takes less space than the code to do the 
	// lookups on the C64 so it saves space in RAM1
	memset(sc_index, NULL_TILE, 64);
	memset(sts_pieceScores, NULL_TILE, sizeof(sts_pieceScores));
	
	for(i=0;i<64;++i)
	{
		piece = gpChessBoard[i];
		if(NONE != piece)
		{
			if((piece & PIECE_WHITE) >> 7 == side)
			{
				// if the piece is on the side the AI is running for, set 
				// index to point back at the iPiece'th number of the piece (iPiece'th
				// being where it was encountered in the scan of the board from tile 0 to 63)
				// and init the sts_pieceScores for that iPiece number
				sc_index[i] = iPiece;
				sts_pieceScores[iPiece].m_piece = piece & PIECE_DATA;
				sts_pieceScores[iPiece].m_source = i;
				sts_pieceScores[iPiece].m_score = 0;
				++iPiece;
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
// This routine is where any code that looks at the global board should
// go. What's here now does only this: a) If the king doesn't have a valid
// move (like the start of the game), encourage any pieces next to the 
// king, on his side, to move so the king has an escape; b) encourage
// pawns to keep moving forward, more so the closer they get to 
// promotion.
// This routine probably needs a lot more to make the chess any good,
// for example looking at freeing up a pawn stuck behind a pawn of its
// own color, or getting a sense for where the pressure will be and 
// putting a defence in place
void cpu_HolisticScore(char side)
{
	int offset;
	char i, nTile, nPiece, scratch, src;

	// Get the king
	src = gKingData[side];
	board_GeneratePossibleMoves(src, 0);

	// If the king has nowhere to go
	if(!gNumMoves)
	{
		// encourage a neighbour to make space for the king
		for(i=0; i<8; ++i)
		{
			// For each neighbour
			nTile = src+scsc_neighbours[i];
			if(nTile<64)
			{
				// See if it's a piece and if the neighbour piece is on the same side as the king
				nPiece = sc_index[nTile];
				if(NULL_TILE != nPiece)
				{
					// See who's attacking it
					offset = giAttackBoardOffset[nTile][1-side];
					scratch = gpAttackBoard[offset];
					// if it has no attackers, encourage it to move
					if(!scratch)
						sts_pieceScores[nPiece].m_score = 4;
					// if the neighbour is a pawn and the pawn has one attacker
					else if(1 == scratch && PAWN == sts_pieceScores[nPiece].m_piece)
					{
						// Get the tile of the attacker
						scratch = gpAttackBoard[offset+1];
						// if it is a vertical (head-on) attack, encourage the pawn to step forward
						if((nTile & 7) == (scratch & 7))
							sts_pieceScores[nPiece].m_score = 4;
					}
				}
			}
		}
	}
	
	// Take a look at the pawns
	for(i=0;i<NUM_PIECES_SIDE;++i)
	{
		if(PAWN == sts_pieceScores[i].m_piece)
		{
			// See if the pawn can move up
			nTile = sts_pieceScores[i].m_source + (side ? -8 : 8);
			if(nTile < 64)
			{
				nPiece = sc_index[nTile];
				if(NULL_TILE == nPiece)
				{
					// See if the next block is under attack
					offset = giAttackBoardOffset[nTile][1-side];
					scratch = gpAttackBoard[offset];
					// if the pawn can move because the square isn't under attack or is well defended
					if(!scratch || scratch < giAttackBoardOffset[nTile][side])
					{
						scratch = (nTile / 8);
						if(side)
							scratch = 7 - scratch;
						
						// encourage the PAWN to move
						sts_pieceScores[i].m_score += scratch;
					}
				}
			}
		}
	}
}

/*-----------------------------------------------------------------------*/
// This routine scores a piece where it stands.	 Positive "score" encourages
// it to move, negative score discourages a move to a new spot
int cpu_SourceScore(char position)
{
	char i, color, piece, value, tile, targetPiece, other, found;
	int score = 0;
	
	// Extract the piece data 
	piece = gpChessBoard[position];
	color = (piece & PIECE_WHITE) >> 7;
	piece &= PIECE_DATA;
	value = gPieceValues[piece];
	other = 1-color;

	// If this piece is under attack increase chance to move, else decrease
	if(gpAttackBoard[giAttackBoardOffset[position][other]])
		score += value;
	else
		score -= 1;
		
	// If this piece is being defended, decrease chance to move, else increase
	if(gpAttackBoard[giAttackBoardOffset[position][color]])
		score -= 1;
	else
		score += 1;

	// Generate moves, including defend/attack moves
	board_GeneratePossibleMoves(position, 1);

	found = 0;
	for(i=0;i<gNumMoves;++i)
	{
		tile = gPossibleMoves[i];
		targetPiece = gpChessBoard[gPossibleMoves[i]];
		if(NONE != targetPiece)
		{
			// Providing support to a piece on own team
			if((targetPiece & PIECE_WHITE)>> 7 == color)
			{
				found = 1;
				// discourage move
				score -= 1;

				// Is the piece being supported under attack
				if(gpAttackBoard[giAttackBoardOffset[tile][other]])
				{
					// Is position the only defender of the piece under attack, then discourace moving
					if(1 == gpAttackBoard[giAttackBoardOffset[tile][color]])
						score -= 2;
					else
						score += 1;

					// If the supported piece is more valuable than the position piece then discourage move
					if(gPieceValues[targetPiece & PIECE_DATA] > value)
						score -= 2;
					else
						score += 1;
				}
			}
		}
	}
	// If not supporting a piece, encourage a move
	if(!found)
		score += 1;
	
	return score;
}

/*-----------------------------------------------------------------------*/
// This routine scores a piece should it move from position to destination
int cpu_DestScore(char position, char destination)
{
	char i, color, piece, value, tile, targetPiece, other;
	int score = 0;

	//	extract the piece at position
	piece = gpChessBoard[position];
	color = (piece & PIECE_WHITE) >> 7;
	piece &= PIECE_DATA;
	value = gPieceValues[piece];
	other = 1-color;

	// If this piece will be under attack decrease chance to move
	if(gpAttackBoard[giAttackBoardOffset[destination][other]])
		score -= value;
		
	// Will be defended
	if(gpAttackBoard[giAttackBoardOffset[destination][color]])
		score += 1;
	else
		score -= 1;
	
	// If a piece is taken at destination, the score is the value of the piece
	targetPiece = gpChessBoard[destination];
	if(NONE != targetPiece && color != ((targetPiece & PIECE_WHITE) >> 7))
		score += gPieceValues[targetPiece & PIECE_DATA];

	// "Move" the piece to destination
	gpChessBoard[destination] = gpChessBoard[position];
	// Generate moves, including defend/attack moves
	board_GeneratePossibleMoves(destination, 1);
	// Restore the destination piece
	gpChessBoard[destination] = targetPiece;

	for(i=0;i<gNumMoves;++i)
	{
		tile = gPossibleMoves[i];
		targetPiece = gpChessBoard[gPossibleMoves[i]];
		if(NONE != targetPiece)
		{
			// Providing support to a piece on my team
			if((targetPiece & PIECE_WHITE)>> 7 == color)
			{
				// encourage move
				score += 1;

				// Is the piece being supported under attack
				if(gpAttackBoard[giAttackBoardOffset[tile][other]])
				{
					// Encourage move
					score += 1;
					
					// Well dest be the only defender of the piece under attack, then encourage moving
					if(1 == gpAttackBoard[giAttackBoardOffset[tile][color]])
						score += 2;

					// If the supported piece is more valuable than the position piece then encourage move
					if(gPieceValues[targetPiece & PIECE_DATA] > value)
						score += 1;
				}
			}
			else
			{
				// Attacking a piece on the other team from dest is encouraged
				score += 1;
				
				// If the piece attacked is of greater value than self
				targetPiece = gPieceValues[targetPiece & PIECE_DATA];
				if(targetPiece > value)
					score += targetPiece - value;
					
				// The piece that will be attacked has no support
				if(!gpAttackBoard[giAttackBoardOffset[tile][other]])
					score += 2;
			}
		}
	}
	
	return score;
}

/*-----------------------------------------------------------------------*/
void cpu_ScorePieceMoves()
{
	char i, j, src, dest;
	int sourceScore;

	// Look at all the pieces on this side, using the support data structure
	for(i=0;i<NUM_PIECES_SIDE;++i)
	{
		// Only check pieces that are still on the board
		src = sts_pieceScores[i].m_source;
		if(NULL_TILE == src)
			break;
		
		// For every piece, make sure the array for scores to every dest is reset
		memset(sts_moveScore, NULL_TILE, sizeof(sts_moveScore));
		
		// Generate all the moves for the piece
		board_GeneratePossibleMoves(src, 0);

		if(!gNumMoves)
			continue;
			
		// Back up the moves since the global buffer is reued
		memcpy(sc_myMoves, gPossibleMoves, gNumMoves);
		sc_numMyMoves = gNumMoves;

		// Calc a score at source
		sourceScore = cpu_SourceScore(src);
		
		for(j = 0; j<sc_numMyMoves; ++j)
		{
			dest = sc_myMoves[j];
			// Sort the scores at each dest + source into the sts_moveScore array
			cpu_AddScoreSorted(sts_moveScore, MAX_PIECE_MOVES, src, dest, sourceScore + cpu_DestScore(src, dest));
		}

		// Start with the top scoring move, moving through the moves, see if 
		// there's a valid move
		j = 0;
		gOutcome = OUTCOME_INVALID;
		while(j < MAX_PIECE_MOVES && NULL_TILE != sts_moveScore[j].m_dest && OUTCOME_INVALID == gOutcome)
		{
			// Prepare to call board_ProcessAction
			gTile[0] = sts_moveScore[j].m_source;
			gTile[1] = sts_moveScore[j].m_dest;
			gPiece[0] = gpChessBoard[gTile[0]];
			gPiece[1] = gpChessBoard[gTile[1]];
			gColor[0] = (gPiece[0] & PIECE_WHITE) >> 7;
			gColor[1] = (gPiece[1] & PIECE_WHITE) >> 7;
			gMove[0] = gPiece[0] & PIECE_MOVED;
			gMove[1] = gPiece[1] & PIECE_MOVED;
			gPiece[0] &= PIECE_DATA;
			gPiece[1] &= PIECE_DATA;

			// See if the move is valid
			if(OUTCOME_INVALID != (gOutcome = board_ProcessAction()))
			{
				// if it was a valid move, add it to undo, and immediately undo the move
				undo_AddMove();
				undo_Undo();
			}
			else
				++j;
		}

		// If a valid move was found, add it as the move for this piece
		if(OUTCOME_INVALID != gOutcome)
		{
			// Add the dest and score as the best (highest scoring) 
			// move for this piece on this side.
			sts_pieceScores[i].m_dest = sts_moveScore[j].m_dest;
			sts_pieceScores[i].m_score += sts_moveScore[j].m_score;
		}
	}
}

/*-----------------------------------------------------------------------*/
// This function looks at all the pieces on a side and finds the top 
// scoring move, tracking the top MAX_PIECE_MOVES moves, one of which 
// is hopefully actually valid
int cpu_FindBestOpponentMove(char side, char *from, char *to)
{
	char i, j, piece, color, move;
	int score;

	memset(sts_moveScore, NULL_TILE, sizeof(sts_moveScore));
	
	for(i=0;i<64;++i)
	{
		piece = gpChessBoard[i];
		if(NONE != piece)
		{
			color = (piece & PIECE_WHITE) >> 7;
			if(side == color)
			{
				// For every piece on "side" generate moves
				board_GeneratePossibleMoves(i, 0);
				
				if(!gNumMoves)
					continue;
					
				memcpy(sc_myMoves, gPossibleMoves, gNumMoves);
				sc_numMyMoves = gNumMoves;

				// Score the source
				score = cpu_SourceScore(i);
				for(j = 0; j<sc_numMyMoves; ++j)
				{
					move = sc_myMoves[j];
					// Sort the scores at each dest + source into the sts_moveScore array
					cpu_AddScoreSorted(sts_moveScore, MAX_PIECE_MOVES, i, move, score + cpu_DestScore(i, move));
				}
			}
		}
	}
	
	j = 0;
	// If deepThinking then validate the moves
	if(gDeepThoughts)
	{
		gOutcome = OUTCOME_INVALID;
		while(j < MAX_PIECE_MOVES && NULL_TILE != sts_moveScore[j].m_dest && OUTCOME_INVALID == gOutcome)
		{
			// Prepare to call board_ProcessAction
			gTile[0] = sts_moveScore[j].m_source;
			gTile[1] = sts_moveScore[j].m_dest;
			gPiece[0] = gpChessBoard[gTile[0]];
			gPiece[1] = gpChessBoard[gTile[1]];
			gColor[0] = (gPiece[0] & PIECE_WHITE) >> 7;
			gColor[1] = (gPiece[1] & PIECE_WHITE) >> 7;
			gMove[0] = gPiece[0] & PIECE_MOVED;
			gMove[1] = gPiece[1] & PIECE_MOVED;
			gPiece[0] &= PIECE_DATA;
			gPiece[1] &= PIECE_DATA;

			// Do the move
			if(OUTCOME_INVALID != (gOutcome = board_ProcessAction()))
			{
				// if it was a valid move, add it to undo, and immediately undo the move
				undo_AddMove();
				undo_Undo();
			}
			else
				++j;
		}
		// If no valid moves are found, return a score of INT_MIN/2
		if(MAX_PIECE_MOVES == j)
			return INT_MIN/2;
	}
	*from = sts_moveScore[j].m_source;
	*to = sts_moveScore[j].m_dest;
	return sts_moveScore[j].m_score;
}

/*-----------------------------------------------------------------------*/
// This function effects a move and then looks for the best move on the
// opposing team.  It will recursively call itself with the outcome
// up to gMaxLevel's.  The sign flips between 1 and -1.	 The score is
// multiplied by the sign so you (1), opponent (-1), you (1), etc. will
// calc a score for your team subtracting victories for the opponent
// in the deeper levels but adding in your own victories in those deeper 
// levels, generating a single comprhensive score for the impact of this move
int cpu_ScorePieceSubTree(char level, char side, signed char sign, char src, char dst)
{
	int score;
	char pieces[2], from, to, col;
	
	// This move is known (maybe only assumed -based on deepThinking)
	// to be valid.	 Back up the pieces and make the move
	pieces[0] = gpChessBoard[src];
	pieces[1] = gpChessBoard[dst];
	// If the king is taken, stop looking
	if(KING == (pieces[1] & PIECE_DATA))
		return gPieceValues[KING];
	
	// Make the move, set the move bit and update the king tracker if needed
	gpChessBoard[src] = NONE;
	gpChessBoard[dst] = pieces[0] | PIECE_MOVED;
	if(KING == (pieces[0] & PIECE_DATA))
		gKingData[(col = (pieces[0] & PIECE_WHITE)>>7)] = dst;
	
	// See if this move creates an en passant opportunity for the opposing team
	gEPPawn = NULL_TILE;
	if(!(pieces[0] & PIECE_MOVED) && PAWN == (pieces[0] & PIECE_DATA))
	{
		gTile[0] = src;
		gTile[1] = dst;
		board_ProcessEnPassant(ENPASSANT_MAYBE);
	}

	// If deep thinking, update the attack db to be accurate
	if(gDeepThoughts)
		board_PlacePieceAttacks();

	// Find what the opponent will likely do
	score = cpu_FindBestOpponentMove(1-side, &from, &to);
	if(score > INT_MIN/2)
	{
		// Invert for opposing side
		score = (score * sign);
		// Then follow that move for gMaxLevel's deep
		if(level < gMaxLevel)
			score += cpu_ScorePieceSubTree(level+1, 1-side, sign ^ 0xfe, from, to);
	}
	else
	{
		// INT_MIN/2 is artificial for a dead-end sub-tree.
		// I haven't thought this through
		score = (INT_MIN/2) * sign;
	}

	// restore the backed up pieces, including possibly the king-tracker
	gpChessBoard[src] = pieces[0];
	gpChessBoard[dst] = pieces[1];
	if(KING == (pieces[0] & PIECE_DATA))
		gKingData[col] = src;
	
	return score;
}

/*-----------------------------------------------------------------------*/
// main entry point for the AI play
char cpu_Play(char side)
{
	t_score ts_pieceScore[NUM_PIECES_SIDE];
	char i, best;
	int iBest = INT_MIN;
	
	// This is realy just to avoid the pop-up for pawn promotion
	gAI = 1;
	
	// Set up some helper structures
	cpu_InitPieceData(side);
	
	// Take a holistic view of the board
	cpu_HolisticScore(side);
	
	// Get a score for all pieces on the AI side
	cpu_ScorePieceMoves();

	memset(ts_pieceScore, NULL_TILE, sizeof(ts_pieceScore));

	// Sort the AI piece's scores by score and validity
	for(i=0;i<NUM_PIECES_SIDE;++i)
		cpu_AddScoreSorted(ts_pieceScore, NUM_PIECES_SIDE, sts_pieceScores[i].m_source, sts_pieceScores[i].m_dest, sts_pieceScores[i].m_score);

	i = 0;
	best = 0;
	// For the best scores up to gWidth, calculate a sub-tree score.  Sub tree is what move will the oponent likely make,
	// then the AI then the oponent, etc., up to gMaxLevel's deep
	for(i=0; i<gWidth; ++i)
	{
		// Only consider valid moves
		if(NULL_TILE == ts_pieceScore[i].m_dest)
			continue;

		// Score the move and its sub-tree
		ts_pieceScore[i].m_score += cpu_ScorePieceSubTree(0, side, -1, ts_pieceScore[i].m_source, ts_pieceScore[i].m_dest);
		// If it's better than the previous best score, consider this the best score
		if(ts_pieceScore[i].m_score >= iBest)
		{
			best = i;
			iBest = ts_pieceScore[i].m_score;
		}
	}
	
	// If deep thinking, update the attack db to be accurate
	if(gDeepThoughts)
		board_PlacePieceAttacks();

	// If the destination move is not valid, there's no valid move so stalemate
	if(NULL_TILE == ts_pieceScore[best].m_dest)
	{
		char tile[2], piece[2], color[2], move[2], outcome;

		// See what the last move was
		undo_FindUndoLine(0);

		// if the outcome was already something other than OK (like check-mate)
		// then nothing more needs happen here
		if(OUTCOME_OK == gOutcome)
		{
			// a hack of sorts to make the outcome in the undo-stack be
			// stalemate so the correct outcome is displayed along the
			// moved that caused it, which was actually the last move
			// the opponent made, but it has only been detected now.
			
			// save the undo state of the last move
			tile[0] = gTile[0];
			tile[1] = gTile[1];
			piece[0] = gPiece[0];
			piece[1] = gPiece[1];
			color[0] = gColor[0];
			color[1] = gColor[1];
			move[0] = gMove[0];
			move[1] = gMove[1];
			outcome = gOutcome;

			// undo the move that caused stalemate
			undo_Undo();
			frontend_LogMove(1);

			// restore that state, but set the outcome to 
			// OUTCOME_STALEMATE
			gTile[0] = tile[0];
			gTile[1] = tile[1];
			gPiece[0] = piece[0]; 
			gPiece[1] = piece[1]; 
			gColor[0] = color[0]; 
			gColor[1] = color[1]; 
			gMove[0] = move[0];
			gMove[1] = move[1];
			board_ProcessAction();
			gOutcome = OUTCOME_STALEMATE;

			// Add this as the move
			undo_AddMove();

			// Log the move to update the display
			frontend_LogMove(0);
		}
		
		// will exit so turn off the flag
		gAI = 0;

		// returning stalemate, even if it's already check-mate is
		// fine as it only means the side-to-go status isn't changed
		return OUTCOME_STALEMATE;
	}

	// Set up to do board_ProcessAction
	gTile[0] = ts_pieceScore[best].m_source;
	gTile[1] = ts_pieceScore[best].m_dest;
	gPiece[0] = gpChessBoard[gTile[0]];
	gPiece[1] = gpChessBoard[gTile[1]];
	gColor[0] = (gPiece[0] & PIECE_WHITE) >> 7;
	gColor[1] = (gPiece[1] & PIECE_WHITE) >> 7;
	gMove[0] = (gPiece[0] & PIECE_MOVED);
	gMove[1] = (gPiece[1] & PIECE_MOVED);
	gPiece[0] &= PIECE_DATA;
	gPiece[1] &= PIECE_DATA;
	
	// The move will have been previously tested so will be valid
	if(OUTCOME_INVALID != (gOutcome = board_ProcessAction()))
	{
		// If no piece is taken, up the counter or reset it to zero
		if(NONE != (gPiece[1] & PIECE_DATA))
			gMoveCounter = 0;
		else if(NUM_MOVES_TO_DRAW == ++gMoveCounter)
			gOutcome = OUTCOME_DRAW;

		// Add the AI move to the undo stack
		undo_AddMove();
		
		// Update the display
		plat_DrawSquare(gTile[0]);
		plat_DrawSquare(gTile[1]);
		if(NULL_TILE != gTile[2])
		{
			plat_DrawSquare(gTile[2]);
			gTile[2] = NULL_TILE;
		}
		if(NULL_TILE != gTile[3])
		{
			plat_DrawSquare(gTile[3]);
			gTile[3] = NULL_TILE;
		}
		
		// Log the move
		frontend_LogMove(0);
	}

	gAI = 0;
	return gOutcome;
}
