/*
 *	search.c
 *	cc65 Chess
 *
 *	See search.h.  The shape is the textbook one, which is the point - the old
 *	engine's problem was never the tuning, it was that there was no search:
 *	cpu_ScorePieceSubTree followed a single predicted line with a branching
 *	factor of one, so a refutation the heuristic did not rank first was never
 *	seen at all.
 *
 *	Two things here are not optional decoration:
 *
 *	  Move ordering.  Alpha-beta only prunes when good moves come first; badly
 *	  ordered, it is barely better than plain minimax.  Captures are tried
 *	  first, best victim against cheapest attacker, then the two killer moves
 *	  that caused a cutoff at this ply before.
 *
 *	  Quiescence.  Stopping the search on a fixed depth means stopping in the
 *	  middle of an exchange and believing the count.  Searching on until the
 *	  position is quiet is what stops the engine hanging pieces at the horizon.
 */

#include "types.h"
#include "engine.h"
#include "eval.h"
#include "search.h"

#ifdef SEARCH_PROFILE
#include "c64profile.h"
#endif

/*-----------------------------------------------------------------------*/
// One shared move arena, carved up a ply at a time.  Quiescence asks
// eng_GenCaptures for the handful of moves it actually searches rather than
// generating everything and discarding the quiet ones, so its plies are now
// narrow and the tail costs a fraction of what it used to.
//
// That turned out to matter for strength and not just for speed.  While
// quiescence took a full-width slice per ply, a long capture chain filled this
// arena and the search bailed out to a static evaluation - measured over the
// 512-game match, 1018 times, with the arena pegged at its 512-entry ceiling.
// Quiescence exists precisely for those sharp positions, so it was giving up
// exactly where it was needed.  The same workload now peaks at 231 entries and
// never runs out.
//
// Running out was never a *correctness* problem - the generator is given its
// capacity and stops - but the fallback silently costs playing strength, which
// is a good deal harder to notice.  Re-measured after the depth caps went up:
// 267 of 512, still never exhausted.  The headroom is worth keeping until a
// target's RAM budget actually needs it
#define SEARCH_ARENA		512

static t_engMove	st_arena[SEARCH_ARENA];
static unsigned int	si_arenaTop;

// Two killers per ply: quiet moves that caused a beta cutoff here before, and
// so are worth trying early in sibling positions
static t_engMove	st_killers[SEARCH_MAX_PLY][2];

static unsigned int	si_nodes;
static unsigned int	si_budget;
static char			sc_abort;

#if SEARCH_RESTORE_UNMAKE
// Four 16-bit values for each reachable move-making ply: 96 bytes, kept out
// of the user undo ring.  A ply's slot is reused only after its child returns.
typedef struct tag_searchState
{
	unsigned int m_hash;
	int m_eval;
	int m_end;
	int m_phase;
} t_searchState;

static t_searchState st_state[SEARCH_MAX_PLY];

/*-----------------------------------------------------------------------*/
static void saveState(char ply)
{
	t_searchState *state = &st_state[ply];

	state->m_hash = geHashKey;
	state->m_eval = geEvalScore;
	state->m_end = geEvalEnd;
	state->m_phase = gePhase;
}

/*-----------------------------------------------------------------------*/
static void restoreState(char ply)
{
	t_searchState *state = &st_state[ply];

	geHashKey = state->m_hash;
	geEvalScore = state->m_eval;
	geEvalEnd = state->m_end;
	gePhase = state->m_phase;
}
#else
#define saveState(ply)
#define restoreState(ply)
#endif

#ifdef SEARCH_PROFILE
// Test-only selector and scratch list for the doubling profiler.  Keeping all
// rows in one binary makes the baseline and candidate pay the same dispatch
// overhead, and makes it possible to interleave them in one emulator run.
char geSearchProfile;
static t_engMove st_profileMoves[127];
#endif

#ifdef EVAL_TUNING
char geSearchRepetition = 1;
char geSearchRandomOpening = 1;
char geSearchCheckEvasion = 1;
#endif

// Opening randomiser state.  Zero means "never seeded", which is how every
// test harness gets the old behaviour to the digit without knowing this is here
static char			sc_rand;
static char			sc_randMoves;

/*-----------------------------------------------------------------------*/
// Most Valuable Victim / Least Valuable Attacker needs pieces ranked by
// worth.  The piece constants in types.h are not in that order, so rank them
// here: indexed by NONE, ROOK, KNIGHT, BISHOP, QUEEN, KING, PAWN
static const char sc_mvvRank[PAWN+1] = { 0, 4, 2, 3, 5, 6, 1 };

/*-----------------------------------------------------------------------*/
// 8 bit Galois LFSR, x^8+x^6+x^5+x^4+1, period 255.  A shift, a test and an
// xor - the whole randomiser has to be affordable on a 1MHz machine, and this
// is about as small as a generator gets.  It never returns to zero from a
// nonzero state, so "seeded" stays true for the life of the game
static char randNext(void)
{
	char lsb = sc_rand & 1;

	sc_rand >>= 1;
	if(lsb)
		sc_rand ^= 0xB8;

	return sc_rand;
}

/*-----------------------------------------------------------------------*/
void search_SetSeed(char seed)
{
	// A zero seed is honoured rather than replaced, and means "play this game
	// without randomisation".  Zero is the LFSR's dead state, so that falls out
	// of the arithmetic for free: randNext returns zero forever, nothing gets
	// perturbed, and the engine plays the game it would have played before any
	// of this existed.  It is also what the terminal build asks for, and what a
	// hardware counter hands over about one game in 256 - where the only
	// consequence is that one game opens the way the old engine did
	sc_rand = seed;
	sc_randMoves = 0;
}

/*-----------------------------------------------------------------------*/
// The same generator, for the game layer's opening table.  Sharing it rather
// than starting a second one keeps the whole program's randomness downstream
// of the single seed plat_GetSeed handed over, so "unseeded plays the old
// game" stays true of everything and not just of the search
char search_Random(void)
{
	return randNext();
}

/*-----------------------------------------------------------------------*/
char search_Seeded(void)
{
	return sc_rand != 0;
}

/*-----------------------------------------------------------------------*/
// How many moves will still fit in the arena, clamped to what a char can hold
static char arenaRoom(void)
{
	unsigned int room = SEARCH_ARENA - si_arenaTop;

	return (room > 127) ? 127 : (char)room;
}

/*-----------------------------------------------------------------------*/
// Times are for an unaccelerated C64; an emulator run at 100x turns the whole
// ladder into a fraction of a second to a few seconds.
//
// **The budget is the weakener; the depth cap is only a safety rail.**  Both
// halves of that were learned by getting them wrong.
//
// First wrong: tuning as though the *budget* set the time.  It does not - the
// search stops when it finishes its deepest iteration, so a level normally
// spends what its depth costs and hands the move back, and the budget binds
// only in expensive positions.  Level 1 used to be depth 2 with 500 nodes,
// but depth 2 wants about 1150 in a middlegame, so from move four it burned
// the whole budget, threw the iteration away and played its depth-1 move -
// fourteen seconds on a C64 to play one ply.  Level 4 was depth 6 with 30000
// and never once reached the depth on its label.
//
// Second wrong, and worse: fixing that by making the *depth cap* the weakener.
// Level 1 became a hard cap of depth 1, which hit its time target and could
// not mate.  Up a queen, a rook and a bishop against a bare king it shuffled a
// rook back and forth until the fifty-move rule drew the game - every quiet
// move scored the same and nothing could see a mate two moves away.
//
// The cap is the wrong lever because **what a depth costs depends on the
// position, and a cap does not**.  Depth 2 wants ~1159 nodes in a middlegame
// but only ~220 in an endgame, so a cap set to keep the middlegame quick also
// blinds the engine in endgames where that same depth was nearly free.  A
// budget scales the right way by construction: with search_Best declining to
// start an iteration it cannot finish, it buys whatever depth the position can
// afford and stops.  Endgames therefore get searched deeper for free, which is
// exactly where the extra depth is needed to convert.
//
// Costs measured by tests/budget.c over a real game, nodes to *complete* each
// depth, from ply 7 on:
//
//   depth 1   mean    152   worst    335      (sharp positions: up to 3228)
//   depth 2   mean   1159   worst   3551
//   depth 3   mean   6720   worst  14712
//   depth 4   mean  28868   worst  51161
//
// Level 1's 400 is a floor, not a preference: at 300 it goes back to shuffling
// a won endgame into a fifty-move draw.  It costs about 8 seconds a move on a
// stock C64 against a 3-5 second target, and that trade is deliberate - a
// "very easy" level that cannot beat a bare king is broken, not easy
const t_searchSkill gcSearchSkill[SEARCH_NUM_SKILLS] =
{
	{ 3,   400 },	// very easy  - measured 8.2s a move on a stock C64
	{ 4,  1200 },	// easy       - measured 29.4s
	{ 5, 15000 },	// harder     - a couple of minutes on bare metal
	{ 6, 60000u },	// very hard  - emulator territory
};

/*-----------------------------------------------------------------------*/
static char isCapture(const t_engMove *move)
{
	return (NONE != (geBoard[move->m_to] & PIECE_DATA)) ||
	       (0 != (move->m_flags & ENG_MF_ENPASSANT));
}

/*-----------------------------------------------------------------------*/
// Scores are only ever compared with each other, so the exact numbers do not
// matter - the ordering does.  Everything fits in the move's char score field.
//
// placeFirst is for internal lists only: track the first highest score (strict
// >, matching pickBest) and swap it to index 0 so the first pickBest scan can
// be skipped.  The root must pass 0 - randomisation and previous-iteration
// priority still rewrite scores afterwards, and placing first here would change
// later tie order among equals.
static void scoreMoves(t_engMove *moves, char count, char ply
#if SEARCH_SCORE_FIRST
	, char placeFirst
#endif
	)
{
	char i;
#if SEARCH_SCORE_FIRST
	char best = 0;
	t_engMove swap;
#endif

	for(i = 0; i < count; ++i)
	{
		char promote = moves[i].m_flags & ENG_MF_PROMO;
		char victim = geBoard[moves[i].m_to] & PIECE_DATA;

		if(promote)
		{
			// a queen promotion is usually the best move on the board
			moves[i].m_score = (QUEEN == promote) ? 200 : 120;
		}
		else if(NONE != victim)
		{
			char attacker = geBoard[moves[i].m_from] & PIECE_DATA;
			moves[i].m_score = 150 + (sc_mvvRank[victim] << 3) - sc_mvvRank[attacker];
		}
		else if(moves[i].m_flags & ENG_MF_ENPASSANT)
			moves[i].m_score = 150 + (1 << 3) - 1;
		else if(ply < SEARCH_MAX_PLY &&
		        ((moves[i].m_from == st_killers[ply][0].m_from &&
		          moves[i].m_to == st_killers[ply][0].m_to) ||
		         (moves[i].m_from == st_killers[ply][1].m_from &&
		          moves[i].m_to == st_killers[ply][1].m_to)))
			moves[i].m_score = 100;
		else
			moves[i].m_score = 0;

#if SEARCH_SCORE_FIRST
		if(placeFirst && moves[i].m_score > moves[best].m_score)
			best = i;
#endif
	}

#if SEARCH_SCORE_FIRST
	if(placeFirst && count && best != 0)
	{
		swap = moves[0];
		moves[0] = moves[best];
		moves[best] = swap;
	}
#endif
}

/*-----------------------------------------------------------------------*/
// Swap the best of the moves still to be tried into position "from".  Picking
// one at a time beats sorting the whole list, because a cutoff usually lands
// after the first few and the rest never get looked at
static void pickBest(t_engMove *moves, char count, char from)
{
	char i, best = from;
	t_engMove swap;

	for(i = from + 1; i < count; ++i)
		if(moves[i].m_score > moves[best].m_score)
			best = i;

	if(best != from)
	{
		swap = moves[from];
		moves[from] = moves[best];
		moves[best] = swap;
	}
}

/*-----------------------------------------------------------------------*/
static void recordKiller(const t_engMove *move, char ply)
{
	if(ply >= SEARCH_MAX_PLY)
		return;
	if(st_killers[ply][0].m_from == move->m_from && st_killers[ply][0].m_to == move->m_to)
		return;

	st_killers[ply][1] = st_killers[ply][0];
	st_killers[ply][0] = *move;
}

/*-----------------------------------------------------------------------*/
// Search on past the horizon, but only through captures, until the position
// stops being violent.  "Stand pat" is the score for declining to capture at
// all, which is the floor - a side is never forced to take
static int quiesce(char side, int alpha, int beta, char ply)
{
	t_engMove *moves;
	t_engUndo undo;
	char count, i, inCheck, legal = 0;
#if ENGINE_FAST_LEGAL
	char wasInCheck;
#endif
	int stand, score;
	unsigned int arenaSave;

	if(si_nodes >= si_budget)
	{
		sc_abort = 1;
		return 0;
	}
	++si_nodes;

	// Being in check changes what this function is allowed to do, and getting
	// that wrong is how the engine came to hang mate in one.  Two things follow
	// from it and both matter:
	//
	//   Standing pat is illegal.  The stand-pat score says "I could decline to
	//   move and still be at least this well off", which is exactly false when
	//   the king is attacked - there is no declining.
	//
	//   Captures are not the only moves worth looking at.  A king walking out
	//   of check is a quiet move, so a search that only generates captures can
	//   conclude a position is quiet when the side to move is being mated.
	//
	// Before this, negamax at depth 0 handed straight over to a capture-only
	// search that stood pat, so mate in one was invisible at depth 1 - and
	// level 1 rarely finishes depth 2 on 400 nodes.  Measured over 60 mate-in-
	// one positions from real games it found 27; with this, 60
#if ENGINE_FAST_LEGAL
	wasInCheck = eng_InCheck(side);
	inCheck = SEARCH_CHECK_EVASION && wasInCheck;
#else
	inCheck = SEARCH_CHECK_EVASION && eng_InCheck(side);
#endif

	if(!inCheck)
	{
		stand = eval_Position(side);
		if(stand >= beta)
			return beta;
		if(stand > alpha)
			alpha = stand;
	}

	// out of ply or out of arena, and in check: there is no stand-pat score to
	// fall back on, so take the static evaluation rather than an alpha that may
	// still be -infinity
	if(ply >= SEARCH_MAX_PLY)
		return inCheck ? eval_Position(side) : alpha;

	arenaSave = si_arenaTop;
	if(arenaRoom() < 8)
		return inCheck ? eval_Position(side) : alpha;
	moves = &st_arena[si_arenaTop];

#ifdef SEARCH_PROFILE
	if(inCheck && PROFILE_GEN_MOVES == geSearchProfile)
		(void)eng_GenMoves(side, st_profileMoves, arenaRoom());
	else if(!inCheck && PROFILE_GEN_CAPTURES == geSearchProfile)
		(void)eng_GenCaptures(side, st_profileMoves, arenaRoom());
#endif

	count = inCheck ? eng_GenMoves(side, moves, arenaRoom())
	                : eng_GenCaptures(side, moves, arenaRoom());
	si_arenaTop += count;

#if SEARCH_SCORE_FIRST
	scoreMoves(moves, count, ply, 1);
#else
	scoreMoves(moves, count, ply);
#endif

#ifdef SEARCH_PROFILE
	if(PROFILE_SCORE == geSearchProfile)
#if SEARCH_SCORE_FIRST
		scoreMoves(moves, count, ply, 1);
#else
		scoreMoves(moves, count, ply);
#endif
#endif

	for(i = 0; i < count; ++i)
	{
#if SEARCH_SCORE_FIRST
		// element 0 is already the best after scoreMoves(..., 1)
		if(i)
#endif
			pickBest(moves, count, i);

#ifdef SEARCH_PROFILE
		if(PROFILE_SELECT == geSearchProfile)
			pickBest(moves, count, i);
		if(PROFILE_BOARD == geSearchProfile)
			eng_ProfileBoardPair(&moves[i]);
#endif

		saveState(ply);
		eng_Make(&moves[i], &undo);

#if ENGINE_FAST_LEGAL
#ifdef SEARCH_PROFILE
		if(PROFILE_LEGALITY == geSearchProfile)
			(void)eng_LeavesInCheck(side, &moves[i], wasInCheck);
#endif
		if(eng_LeavesInCheck(side, &moves[i], wasInCheck))
#else
#ifdef SEARCH_PROFILE
		if(PROFILE_LEGALITY == geSearchProfile)
			(void)eng_IsAttacked(geKing[side], 1 - side);
#endif
		if(eng_IsAttacked(geKing[side], 1 - side))
#endif
		{
			eng_Unmake(&moves[i], &undo);
			restoreState(ply);
			continue;
		}
		++legal;

		score = -quiesce(1 - side, -beta, -alpha, ply + 1);
		eng_Unmake(&moves[i], &undo);
		restoreState(ply);

		if(sc_abort)
			break;

		if(score >= beta)
		{
			si_arenaTop = arenaSave;
			return beta;
		}
		if(score > alpha)
			alpha = score;
	}

	si_arenaTop = arenaSave;

	// In check with nothing legal is mate, and saying so is the whole reason
	// the evasions above are generated.  Not after an abort: the move list was
	// abandoned part way, so "no legal move found" means the budget ran out and
	// not that the side is mated
	if(inCheck && !legal && !sc_abort)
		return -EVAL_MATE_IN(ply);

	return alpha;
}

/*-----------------------------------------------------------------------*/
static int negamax(char side, char depth, int alpha, int beta, char ply)
{
	t_engMove *moves;
	t_engUndo undo;
	char count, i, legal = 0, inCheck;
	int score;
	unsigned int arenaSave;

	if(si_nodes >= si_budget)
	{
		sc_abort = 1;
		return 0;
	}
	++si_nodes;

	// A position already seen is a draw, and one repeat is enough to say so
	// here rather than the three a real game needs.  If a line comes back to
	// a position both sides could have reached earlier, neither is making
	// progress, and waiting for the third occurrence only means searching the
	// same shuffle twice more before reaching the same answer.
	//
	// This is the whole point of the exercise.  Without it the search cannot
	// tell a won position from the same won position two moves later, so a
	// side with nothing better to do repeats happily - measured over 512
	// self-play games, 318 draws, 57% of them in positions the engine itself
	// scored as winning

#ifdef SEARCH_PROFILE
	if(SEARCH_REPETITION && PROFILE_REPETITION == geSearchProfile)
		(void)eng_IsRepetition(1);
#endif

	if(SEARCH_REPETITION && eng_IsRepetition(1))
		return 0;

	// the fifty move rule, so the search cannot convince itself that shuffling
	// forever is winning
	if(geHalfmove >= 100)
		return 0;

	if(0 == depth)
	{
#if SEARCH_QUIESCE_HISTORY
		return quiesce(side, alpha, beta, ply);
#else
		int quietScore;

		eng_HistoryEnable(0);
		quietScore = quiesce(side, alpha, beta, ply);
		eng_HistoryEnable(1);
		return quietScore;
#endif
	}

	inCheck = eng_InCheck(side);

	arenaSave = si_arenaTop;
	if(arenaRoom() < 8)
		return eval_Position(side);
	moves = &st_arena[si_arenaTop];

#ifdef SEARCH_PROFILE
	if(PROFILE_GEN_MOVES == geSearchProfile)
		(void)eng_GenMoves(side, st_profileMoves, arenaRoom());
#endif

	count = eng_GenMoves(side, moves, arenaRoom());
	si_arenaTop += count;

#if SEARCH_SCORE_FIRST
	scoreMoves(moves, count, ply, 1);
#else
	scoreMoves(moves, count, ply);
#endif

#ifdef SEARCH_PROFILE
	if(PROFILE_SCORE == geSearchProfile)
#if SEARCH_SCORE_FIRST
		scoreMoves(moves, count, ply, 1);
#else
		scoreMoves(moves, count, ply);
#endif
#endif

	for(i = 0; i < count; ++i)
	{
#if SEARCH_SCORE_FIRST
		if(i)
#endif
			pickBest(moves, count, i);

#ifdef SEARCH_PROFILE
		if(PROFILE_SELECT == geSearchProfile)
			pickBest(moves, count, i);
		if(PROFILE_BOARD == geSearchProfile)
			eng_ProfileBoardPair(&moves[i]);
#endif

		saveState(ply);
		eng_Make(&moves[i], &undo);

#if ENGINE_FAST_LEGAL
#ifdef SEARCH_PROFILE
		if(PROFILE_LEGALITY == geSearchProfile)
			(void)eng_LeavesInCheck(side, &moves[i], inCheck);
#endif
		if(eng_LeavesInCheck(side, &moves[i], inCheck))
#else
#ifdef SEARCH_PROFILE
		if(PROFILE_LEGALITY == geSearchProfile)
			(void)eng_IsAttacked(geKing[side], 1 - side);
#endif
		if(eng_IsAttacked(geKing[side], 1 - side))
#endif
		{
			eng_Unmake(&moves[i], &undo);
			restoreState(ply);
			continue;
		}
		++legal;

#if SEARCH_CHECK_EXT
		{
			char nextDepth = (char)(depth - 1);

			// one ply of check extension: search the same remaining depth
			// rather than reducing, so mates in check stay inside the horizon
			if(inCheck && ply + 1 < SEARCH_MAX_PLY)
				nextDepth = depth;

			score = -negamax(1 - side, nextDepth, -beta, -alpha, ply + 1);
		}
#else
		score = -negamax(1 - side, depth - 1, -beta, -alpha, ply + 1);
#endif
		eng_Unmake(&moves[i], &undo);
		restoreState(ply);

		if(sc_abort)
		{
			si_arenaTop = arenaSave;
			return 0;
		}

		if(score >= beta)
		{
			// a quiet move good enough to cut off here is worth trying first
			// in the sibling positions
			if(!isCapture(&moves[i]))
				recordKiller(&moves[i], ply);
			si_arenaTop = arenaSave;
			return beta;
		}
		if(score > alpha)
			alpha = score;
	}

	si_arenaTop = arenaSave;

	// No legal move at all: mated if the king is attacked, stalemate if not.
	// This is the whole of the old board_CheckForMate, board_UpdateAttackGrid
	// and board_CheckLineAttack, and it needs no attack database
	if(!legal)
		return inCheck ? -EVAL_MATE_IN(ply) : 0;

	return alpha;
}

/*-----------------------------------------------------------------------*/
// Uses the shared arena rather than a local array: a move list is 512 bytes
// and cc65 allows a function only 256 bytes of locals, so a board-sized array
// on the stack does not survive contact with the target
char search_Outcome(char side)
{
	t_engMove *moves;
	t_engUndo undo;
	char count, i;
	unsigned int arenaSave = si_arenaTop;

	if(arenaRoom() < 8)
		return OUTCOME_OK;
	moves = &st_arena[si_arenaTop];
	count = eng_GenMoves(side, moves, arenaRoom());
	si_arenaTop += count;

#if ENGINE_FAST_LEGAL
	{
		char wasInCheck = eng_InCheck(side);

		for(i = 0; i < count; ++i)
		{
			char legal;

			eng_Make(&moves[i], &undo);
			legal = !eng_LeavesInCheck(side, &moves[i], wasInCheck);
			eng_Unmake(&moves[i], &undo);

			if(legal)
			{
				si_arenaTop = arenaSave;
				return OUTCOME_OK;
			}
		}
	}
#else
	for(i = 0; i < count; ++i)
	{
		char legal;

		eng_Make(&moves[i], &undo);
		legal = !eng_IsAttacked(geKing[side], 1 - side);
		eng_Unmake(&moves[i], &undo);

		if(legal)
		{
			si_arenaTop = arenaSave;
			return OUTCOME_OK;
		}
	}
#endif

	si_arenaTop = arenaSave;
	return eng_InCheck(side) ? OUTCOME_CHECKMATE : OUTCOME_STALEMATE;
}

/*-----------------------------------------------------------------------*/
static int searchRoot(char side, char depth, t_searchResult *result)
{
	t_engMove *moves;
	t_engUndo undo;
	char count, i, legal = 0;
#if ENGINE_FAST_LEGAL
	char wasInCheck;
#endif
	int alpha = -EVAL_INFINITY, score;
	unsigned int arenaSave = si_arenaTop;

	moves = &st_arena[si_arenaTop];

#ifdef SEARCH_PROFILE
	if(PROFILE_GEN_MOVES == geSearchProfile)
		(void)eng_GenMoves(side, st_profileMoves, arenaRoom());
#endif

	count = eng_GenMoves(side, moves, arenaRoom());
	si_arenaTop += count;

	// Root must use plain scoring: randomisation and the previous iteration's
	// move still adjust scores after this, so placing the best now would change
	// later tie order among equals.
#if SEARCH_SCORE_FIRST
	scoreMoves(moves, count, 0, 0);
#else
	scoreMoves(moves, count, 0);
#endif

#ifdef SEARCH_PROFILE
	if(PROFILE_SCORE == geSearchProfile)
#if SEARCH_SCORE_FIRST
		scoreMoves(moves, count, 0, 0);
#else
		scoreMoves(moves, count, 0);
#endif
#endif

	// Break ties randomly for the first few moves of a game.  Alpha-beta
	// returns the same move whatever order the moves are tried in, with one
	// exception: among moves that score exactly equal the first one searched
	// wins, because the test at the bottom of the loop is a strict >.  So
	// perturbing the ordering score can only ever change which of several
	// equally-best moves gets played - it cannot make the engine prefer a
	// worse one.  That is the whole feature: the opening was identical every
	// game because ties fell to whatever the generator happened to emit first,
	// which is a property of the loop order and not of chess.
	//
	// Deliberately a small perturbation.  Move ordering is what makes
	// alpha-beta cut, so shuffling it wholesale would cost nodes and buy
	// nothing; +0..3 only reorders moves that were already ranked together.
	// The 252 guard keeps it below the 255 the previous iteration's best is
	// about to be given, and stops the addition wrapping a char
	if(sc_rand && SEARCH_RANDOM_OPENING && sc_randMoves < SEARCH_RANDOM_MOVES)
		for(i = 0; i < count; ++i)
			if(moves[i].m_score < 252)
				moves[i].m_score += randNext() & 3;

	// the best move from the previous iteration is the best guess for this
	// one, so try it first - that is most of what iterative deepening buys
	if(result->m_haveMove)
		for(i = 0; i < count; ++i)
			if(moves[i].m_from == result->m_move.m_from &&
			   moves[i].m_to == result->m_move.m_to &&
			   moves[i].m_flags == result->m_move.m_flags)
			{
				moves[i].m_score = 255;
				break;
			}

#if ENGINE_FAST_LEGAL
	wasInCheck = eng_InCheck(side);
#endif
	for(i = 0; i < count; ++i)
	{
		pickBest(moves, count, i);

#ifdef SEARCH_PROFILE
		if(PROFILE_SELECT == geSearchProfile)
			pickBest(moves, count, i);
		if(PROFILE_BOARD == geSearchProfile)
			eng_ProfileBoardPair(&moves[i]);
#endif

		saveState(0);
		eng_Make(&moves[i], &undo);

#if ENGINE_FAST_LEGAL
#ifdef SEARCH_PROFILE
		if(PROFILE_LEGALITY == geSearchProfile)
			(void)eng_LeavesInCheck(side, &moves[i], wasInCheck);
#endif
		if(eng_LeavesInCheck(side, &moves[i], wasInCheck))
#else
#ifdef SEARCH_PROFILE
		if(PROFILE_LEGALITY == geSearchProfile)
			(void)eng_IsAttacked(geKing[side], 1 - side);
#endif
		if(eng_IsAttacked(geKing[side], 1 - side))
#endif
		{
			eng_Unmake(&moves[i], &undo);
			restoreState(0);
			continue;
		}

		// Bank a legal move the instant one is known, *before* searching it.
		// The budget can run out inside this move's subtree, and if that
		// happens on the very first move then everything below is skipped and
		// nothing gets recorded at all.  Callers read "no move" as stalemate,
		// so the engine would abandon a sound position rather than admit it
		// ran out of time - which is exactly what it did on a real board.
		// Move ordering has already put the most promising move first, so this
		// is a reasonable move and not an arbitrary one
		if(!result->m_haveMove)
		{
			result->m_move = moves[i];
			result->m_haveMove = 1;
		}

		score = -negamax(1 - side, depth - 1, -EVAL_INFINITY, -alpha, 1);
		eng_Unmake(&moves[i], &undo);
		restoreState(0);

		if(sc_abort)
			break;

		if(!legal || score > alpha)
		{
			alpha = score;
			result->m_move = moves[i];
			result->m_haveMove = 1;
		}
		++legal;
	}

	si_arenaTop = arenaSave;

	// No reset of m_haveMove here.  It is set above the moment a legal move is
	// found, so it already means exactly "this side has a move", which is what
	// the caller tests for mate and stalemate.  Clearing it on !legal would
	// undo the banked move whenever the budget ran out inside the first move's
	// subtree - "legal" counts searches that *finished*, which is a different
	// question from whether a legal move exists
	return alpha;
}

/*-----------------------------------------------------------------------*/
void search_Best(char side, char maxDepth, unsigned int nodeBudget, t_searchResult *result)
{
	char depth, ply;
	t_searchResult working;

	si_nodes = 0;
	si_budget = nodeBudget;
	si_arenaTop = 0;
	sc_abort = 0;
#if SEARCH_RESTORE_UNMAKE
	eng_RestoreEnable(1);
#endif

	// the running evaluation is maintained by make/unmake, but the position we
	// are handed may have been put together some other way - a new game, an
	// undo, a FEN in a test.  One walk of the board per move is nothing next to
	// the search, and it means the search can never inherit a stale total
	eval_Refresh();

	for(ply = 0; ply < SEARCH_MAX_PLY; ++ply)
	{
		st_killers[ply][0].m_from = st_killers[ply][0].m_to = ENG_NO_SQUARE;
		st_killers[ply][1].m_from = st_killers[ply][1].m_to = ENG_NO_SQUARE;
	}

	result->m_haveMove = 0;
	result->m_score = 0;
	result->m_depth = 0;
	result->m_nodes = 0;

	working = *result;

	for(depth = 1; depth <= maxDepth; ++depth)
	{
		int score;

		// Do not start an iteration there is no hope of finishing.  An
		// abandoned iteration is pure cost - its work is thrown away and the
		// shallower move is played anyway - so spending the last of the budget
		// on one is the worst thing the search can do with it.  Measured
		// growth from tests/budget.c is about 5x a depth; 3x is the cautious
		// version of that test, so a depth that might just finish still gets
		// its chance.  Depth 1 always runs, because a move has to come back.
		//
		// Divide rather than multiply.  cc65's int is 16 bits, so 3 * si_nodes
		// wraps above 21845 - at level 4's 60000-node budget the test silently
		// inverted and the guard never fired at all, on the target only.  The
		// host build has 32-bit ints, so no native test can ever catch this
		if(depth > 1 && si_nodes > (si_budget / 3))
			break;

		score = searchRoot(side, depth, &working);

		// an aborted iteration is incomplete, so keep the last one that
		// finished
		if(sc_abort)
			break;

		*result = working;
		result->m_score = score;
		result->m_depth = depth;

		// no point searching deeper once a forced mate is found
		if(score >= EVAL_MATE_IN(SEARCH_MAX_PLY) || score <= -EVAL_MATE_IN(SEARCH_MAX_PLY))
			break;
	}

	// A budget too small to finish even depth 1 used to return "no move at
	// all", which every caller reads as stalemate - the engine would resign a
	// perfectly good position rather than play slowly.  A partial root scan
	// still holds the best move it managed to look at, and that is always a
	// legal move, so prefer it to nothing
	if(!result->m_haveMove && working.m_haveMove)
	{
		*result = working;
		result->m_depth = 0;
	}

	result->m_nodes = si_nodes;

	// one more of this game's moves is behind us.  Counted here rather than in
	// the caller so that every entry point gets it, and clamped so the count
	// cannot wrap all the way round and switch the randomiser back on in the
	// middle of an endgame
	if(sc_randMoves < SEARCH_RANDOM_MOVES)
		++sc_randMoves;
#if SEARCH_RESTORE_UNMAKE
	eng_RestoreEnable(0);
#endif
}

#ifdef EVAL_TUNING
/*-----------------------------------------------------------------------*/
// Classic scoring without first placement - the baseline pickBest starts from.
static void scoreMovesClassic(t_engMove *moves, char count, char ply)
{
	char i;

	for(i = 0; i < count; ++i)
	{
		char promote = moves[i].m_flags & ENG_MF_PROMO;
		char victim = geBoard[moves[i].m_to] & PIECE_DATA;

		if(promote)
			moves[i].m_score = (QUEEN == promote) ? 200 : 120;
		else if(NONE != victim)
		{
			char attacker = geBoard[moves[i].m_from] & PIECE_DATA;
			moves[i].m_score = 150 + (sc_mvvRank[victim] << 3) - sc_mvvRank[attacker];
		}
		else if(moves[i].m_flags & ENG_MF_ENPASSANT)
			moves[i].m_score = 150 + (1 << 3) - 1;
		else if(ply < SEARCH_MAX_PLY &&
		        ((moves[i].m_from == st_killers[ply][0].m_from &&
		          moves[i].m_to == st_killers[ply][0].m_to) ||
		         (moves[i].m_from == st_killers[ply][1].m_from &&
		          moves[i].m_to == st_killers[ply][1].m_to)))
			moves[i].m_score = 100;
		else
			moves[i].m_score = 0;
	}
}

/*-----------------------------------------------------------------------*/
// Prove the ordered try sequence is identical, not only the eventual best move.
// Selection is a full pass so every index is comparable; search usually stops
// early after a cutoff, but the sequence up to that point is the same algorithm.
char search_TestOrderSequence(char side, char capturesOnly)
{
	t_engMove classic[ENG_MAX_MOVES];
	t_engMove fused[ENG_MAX_MOVES];
	char count, i, ply = 1;

	count = capturesOnly
		? eng_GenCaptures(side, classic, ENG_MAX_MOVES)
		: eng_GenMoves(side, classic, ENG_MAX_MOVES);
	if(count < 2)
		return 1;

	// Plant a quiet move as a killer so the 100-score branch is exercised when
	// full generation is used; captures-only lists rarely have quiets.
	if(!capturesOnly)
	{
		for(i = 0; i < count; ++i)
		{
			if(NONE == (geBoard[classic[i].m_to] & PIECE_DATA) &&
			   0 == (classic[i].m_flags & (ENG_MF_ENPASSANT | ENG_MF_PROMO)))
			{
				st_killers[ply][0] = classic[i];
				st_killers[ply][1].m_from = st_killers[ply][1].m_to = ENG_NO_SQUARE;
				break;
			}
		}
	}

	for(i = 0; i < count; ++i)
		fused[i] = classic[i];

	scoreMovesClassic(classic, count, ply);
	for(i = 0; i < count; ++i)
		pickBest(classic, count, i);

#if SEARCH_SCORE_FIRST
	scoreMoves(fused, count, ply, 1);
#else
	scoreMoves(fused, count, ply);
#endif
	for(i = 0; i < count; ++i)
	{
#if SEARCH_SCORE_FIRST
		if(i)
#endif
			pickBest(fused, count, i);
	}

	for(i = 0; i < count; ++i)
	{
		if(classic[i].m_from != fused[i].m_from ||
		   classic[i].m_to != fused[i].m_to ||
		   classic[i].m_flags != fused[i].m_flags ||
		   classic[i].m_score != fused[i].m_score)
			return 0;
	}
	return 1;
}

/*-----------------------------------------------------------------------*/
char search_TestQuiesceState(char side, unsigned int budget, char exhaustArena)
{
	static char board[128];
	unsigned int hash, history;
	int score, end, phase;
	char ep, castle, halfmove, kingBlack, kingWhite, sq, same = 1;

	eval_Refresh();
	hash = geHashKey;
	history = eng_HistoryStateDigest();
	score = geEvalScore;
	end = geEvalEnd;
	phase = gePhase;
	ep = geEP;
	castle = geCastle;
	halfmove = geHalfmove;
	kingBlack = geKing[SIDE_BLACK];
	kingWhite = geKing[SIDE_WHITE];
	for(sq = 0; sq < 128; ++sq)
		board[sq] = geBoard[sq];

	si_nodes = 0;
	si_budget = budget;
	si_arenaTop = exhaustArena ? SEARCH_ARENA - 4 : 0;
	sc_abort = 0;
	eng_RestoreEnable(1);
	eng_HistoryEnable(0);
	(void)quiesce(side, -EVAL_INFINITY, EVAL_INFINITY, 0);
	eng_HistoryEnable(1);
	eng_RestoreEnable(0);
	si_arenaTop = 0;

	for(sq = 0; sq < 128; ++sq)
		if(board[sq] != geBoard[sq])
			same = 0;
	return same && hash == geHashKey && history == eng_HistoryStateDigest() &&
	       score == geEvalScore && end == geEvalEnd && phase == gePhase &&
	       ep == geEP && castle == geCastle && halfmove == geHalfmove &&
	       kingBlack == geKing[SIDE_BLACK] && kingWhite == geKing[SIDE_WHITE];
}
#endif
