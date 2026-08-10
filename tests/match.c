/*
 *	match.c
 *	cc65 Chess - test support
 *
 *	The instrument Phase 4 tunes with.  Two configurations play each other from
 *	a set of openings, each opening twice with the colours swapped, and the
 *	score comes back as wins/losses/draws.
 *
 *	The opening set is the whole point.  Both sides are deterministic, so from
 *	the initial position they play the same game every time and a hundred games
 *	tell you exactly what one game tells you.  Sixteen games from eight
 *	different openings is a real, if small, sample.
 *
 *	How to read the result: a change is worth keeping if it wins clearly over
 *	the set.  With sixteen games a one game edge is noise - look for something
 *	like 10-4-2 or better before believing it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "undo.h"
#include "board.h"
#include "eval.h"
#include "search.h"
#include "testutil.h"

/*-----------------------------------------------------------------------*/
// The opening set is generated rather than written out: a few plies of random
// legal moves from the start, one fixed seed per opening, keeping only
// positions where neither side is already ahead.  That gives as much variety
// as is wanted, reproducibly, without a page of hand-typed FENs to get wrong.
#define NUM_OPENINGS	256
#define OPENING_PLIES	4

typedef struct tag_Position
{
	char	m_board[128];
	char	m_castle;
	char	m_ep;
	char	m_halfmove;
	char	m_king[2];
	char	m_side;
} t_Position;

static t_Position st_openings[NUM_OPENINGS];
static char sc_openingsBuilt;
static char sc_useEndgames;

/*-----------------------------------------------------------------------*/
static void savePosition(t_Position *pos, char side)
{
	memcpy(pos->m_board, geBoard, 128);
	pos->m_castle = geCastle;
	pos->m_ep = geEP;
	pos->m_halfmove = geHalfmove;
	pos->m_king[0] = geKing[0];
	pos->m_king[1] = geKing[1];
	pos->m_side = side;
}

static char loadPosition(const t_Position *pos)
{
	memcpy(geBoard, pos->m_board, 128);
	geCastle = pos->m_castle;
	geEP = pos->m_ep;
	geHalfmove = pos->m_halfmove;
	geKing[0] = pos->m_king[0];
	geKing[1] = pos->m_king[1];

	// a saved position dropped straight onto the board carries no history
	// with it, and the previous game's is still in the ring
	eng_HashReset();

	return pos->m_side;
}

/*-----------------------------------------------------------------------*/
// plies: how deep to wander before saving.  A handful of plies gives opening
// positions; sixty-odd with captures allowed gives thinned-out endings, which
// is the only way to measure a term that only applies once the queens are off
static void buildPositions(int plies, char allowCaptures)
{
	int i;

	for(i = 0; i < NUM_OPENINGS; ++i)
	{
		char side = SIDE_WHITE;
		int ply;

		srand(1000 + i);
		eng_SetStartPosition();

		for(ply = 0; ply < plies; ++ply)
		{
			t_engMove moves[ENG_MAX_MOVES];
			t_engUndo undo;
			char count = eng_GenMoves(side, moves, ENG_MAX_MOVES);
			char tries, done = 0;

			for(tries = 0; tries < count * 2 && !done; ++tries)
			{
				char pick = rand() % count;

				eng_Make(&moves[pick], &undo);
				if(eng_IsAttacked(geKing[side], 1 - side) ||
				   (!allowCaptures && NONE != (undo.m_captured & PIECE_DATA)))
					eng_Unmake(&moves[pick], &undo);
				else
					done = 1;
			}
			if(!done)
				break;
			side = 1 - side;
		}

		savePosition(&st_openings[i], side);
	}
}

/*-----------------------------------------------------------------------*/
static void buildOpenings(void)
{
	if(sc_openingsBuilt != 1)
	{
		buildPositions(OPENING_PLIES, 0);
		sc_openingsBuilt = 1;
	}
}

static void buildEndgames(void)
{
	if(sc_openingsBuilt != 2)
	{
		buildPositions(70, 1);
		sc_openingsBuilt = 2;
	}
}

/*-----------------------------------------------------------------------*/
typedef struct tag_Config
{
	const char		*m_name;
	char			 m_terms;		// EVAL_* mask
	char			 m_depth;
	unsigned int	 m_nodes;
	char			 m_repetition;	// score a repeated position as a draw
} t_Config;

/*-----------------------------------------------------------------------*/
// Material balance in hundredths of a pawn, white positive.  Walking 64
// squares per ply would be absurd inside the search and is nothing in a
// harness, and it has to be counted rather than read from geEvalScore: the
// evaluation is the thing under test, so it cannot also be the instrument
static int materialBalance(void)
{
	int score = 0;
	char sq;

	for(sq = 0; sq < 0x78; ++sq)
	{
		char piece = geBoard[sq];

		if(ENG_OFFBOARD(sq) || NONE == (piece & PIECE_DATA))
			continue;
		if(piece & PIECE_WHITE)
			score += gcPieceValue[piece & PIECE_DATA];
		else
			score -= gcPieceValue[piece & PIECE_DATA];
	}
	return score;
}

/*-----------------------------------------------------------------------*/
// Conversion: of the games where a side was ever a clear piece up, how many
// did that side actually win?
//
// This is the number that says whether the engine can finish, and W-L-D does
// not contain it - a change can leave the score untouched and still turn won
// endings into draws, which is exactly the failure that started this.  The
// threshold is material because material is objective; an evaluation that
// rates its own position +2.55 when it is +9.8 cannot be asked to referee.
//
// The advantage has to *last* to count.  A piece hanging for one ply before it
// is recaptured is a peak of +3 and means nothing; counting those put 880
// winning sides in 512 games, which is not what anyone means by winning.  Ten
// plies is five moves of actually being a piece up
#define CONVERT_MARGIN		300
#define CONVERT_PLIES		10

static int si_hadWin, si_converted, si_stillUp, si_gaveBack, si_threwIt;

/*-----------------------------------------------------------------------*/
// +1 if "a" won, -1 if "b" won, 0 for a draw or an unfinished game
static int si_drawFifty, si_drawStale, si_drawUnfinished, si_drawRepeat;

static int playGame(const t_Position *opening, const t_Config *a, const t_Config *b,
                    char aSide, int maxPlies, unsigned long *nodesOut)
{
	char side = loadPosition(opening);
	int ply, upWhite = 0, upBlack = 0, verdict;

	undo_Init();
	board_SyncDisplay();

	for(ply = 0; ply < maxPlies; ++ply)
	{
		const t_Config *cfg = (side == aSide) ? a : b;
		t_searchResult result;
		char outcome = search_Outcome(side);

		{
			int material = materialBalance();

			if(material >= CONVERT_MARGIN) ++upWhite;
			else if(-material >= CONVERT_MARGIN) ++upBlack;
		}

		if(OUTCOME_CHECKMATE == outcome)
			goto decided;
		if(OUTCOME_STALEMATE == outcome) { ++si_drawStale; goto drawn; }
		if(geHalfmove >= 100)            { ++si_drawFifty; goto drawn; }
		// the referee's job, and until now the harness did not do it: a
		// threefold ended these games all along, and they were being counted
		// as having hit the ply limit
		if(eng_IsRepetition(2))          { ++si_drawRepeat; goto drawn; }

		geEvalTerms = cfg->m_terms;
		geSearchRepetition = cfg->m_repetition;
		search_Best(side, cfg->m_depth, cfg->m_nodes, &result);
		*nodesOut += result.m_nodes;

		if(!result.m_haveMove)
		{
			++si_drawStale;
			goto drawn;
		}

		board_ApplyMove(&result.m_move, side);
		side = 1 - side;
	}

	++si_drawUnfinished;
drawn:
	// A side that was a clear piece up for long enough and did not win it.
	// Two very different failures hide in that one number, so split them: is
	// it still a piece up at the end and simply cannot finish, or did it hand
	// the material back?  The first wants endgame technique, the second wants
	// something else entirely, and building the wrong one is easy
	{
		int final = materialBalance();

		if(upWhite >= CONVERT_PLIES)
		{
			++si_hadWin;
			if(final >= CONVERT_MARGIN) ++si_stillUp; else ++si_gaveBack;
		}
		if(upBlack >= CONVERT_PLIES)
		{
			++si_hadWin;
			if(-final >= CONVERT_MARGIN) ++si_stillUp; else ++si_gaveBack;
		}
	}
	return 0;

decided:
	// the side to move is mated, so the other side won
	verdict = (side == aSide) ? -1 : 1;
	{
		int winnerUp = (side == SIDE_WHITE) ? upBlack : upWhite;
		int loserUp  = (side == SIDE_WHITE) ? upWhite : upBlack;

		if(winnerUp >= CONVERT_PLIES) { ++si_hadWin; ++si_converted; }
		// a piece up for ten plies and mated anyway: the third failure
		if(loserUp >= CONVERT_PLIES)  { ++si_hadWin; ++si_threwIt; }
	}
	return verdict;
}

/*-----------------------------------------------------------------------*/
static int runMatch(const t_Config *a, const t_Config *b, int maxPlies, int verbose)
{
	int i, wins = 0, losses = 0, draws = 0;
	unsigned long nodes = 0;
	clock_t started = clock();

	if(sc_useEndgames) buildEndgames(); else buildOpenings();
	si_drawFifty = si_drawStale = si_drawUnfinished = si_drawRepeat = 0;
	si_hadWin = si_converted = si_stillUp = si_gaveBack = si_threwIt = 0;
	printf("  %s  vs  %s\n", a->m_name, b->m_name);

	for(i = 0; i < NUM_OPENINGS; ++i)
	{
		int g;

		for(g = 0; g < 2; ++g)
		{
			// g 0: a is white.  g 1: a is black, same opening
			int outcome = playGame(&st_openings[i], a, b,
			                       g ? SIDE_BLACK : SIDE_WHITE, maxPlies, &nodes);

			if(outcome > 0) ++wins;
			else if(outcome < 0) ++losses;
			else ++draws;

			if(verbose)
				printf("    opening %d, %s as %s: %s\n", i, a->m_name,
				       g ? "black" : "white",
				       outcome > 0 ? "win" : outcome < 0 ? "loss" : "draw");
		}
	}

	printf("    %d-%d-%d (W-L-D) over %d games, %lu nodes, %.1fs\n",
	       wins, losses, draws, NUM_OPENINGS * 2, nodes,
	       (double)(clock() - started) / CLOCKS_PER_SEC);
	if(draws)
		printf("    draws: %d threefold, %d fifty-move, %d stalemate, %d hit the %d ply limit\n",
		       si_drawRepeat, si_drawFifty, si_drawStale, si_drawUnfinished, maxPlies);
	if(si_hadWin)
		printf("    conversion: %d of %d sides a clear piece up for %d+ plies won it (%d%%)\n",
		       si_converted, si_hadWin, CONVERT_PLIES,
		       (100 * si_converted) / si_hadWin);
	if(si_hadWin - si_converted)
		printf("      of the %d that did not: %d drew still a piece up, "
		       "%d drew after giving it back, %d lost\n",
		       si_hadWin - si_converted, si_stillUp, si_gaveBack, si_threwIt);

	geEvalTerms = EVAL_ALL;
	geSearchRepetition = 1;
	return wins - losses;
}

/*-----------------------------------------------------------------------*/
// A configuration playing itself must come out level: the same eight games
// twice with the colours swapped, so every result has its mirror.  If this is
// not balanced the harness is measuring something other than the change
int test_RunMatchSanity(int verbose)
{
	t_Config same = { "all-terms", EVAL_ALL, 3, 2000, 1 };
	int edge;

	printf("match sanity: a configuration against itself\n");
	edge = runMatch(&same, &same, 240, verbose);

	if(edge != 0)
	{
		printf("  -> FAIL: self-play is not balanced (edge %+d)\n", edge);
		return 1;
	}
	printf("  -> ok, balanced\n");
	return 0;
}

/*-----------------------------------------------------------------------*/
// The evaluation terms are only free if nodes are free, and they are not.
// Measured on a real C64 (tests/c64search.c), the pawn-structure term makes
// every node about 1.35x more expensive, so in the time the simpler evaluation
// searches 2000 nodes the richer one searches about 1480.  That is the
// comparison the player actually experiences
// The endgame king table can only show up in an endgame, and games started
// from an opening rarely get there before the ply limit
// Each skill level must actually beat the one below it, or the ladder is a lie
int test_RunMatchLadder(int verbose)
{
	char level;

	printf("match: does each skill level beat the one below it?\n");
	for(level = 1; level < SEARCH_NUM_SKILLS; ++level)
	{
		char nameHi[24], nameLo[24];
		t_Config hi, lo;

		sprintf(nameHi, "level %d", level + 1);
		sprintf(nameLo, "level %d", level);
		hi.m_name = nameHi; hi.m_terms = EVAL_ALL;
		hi.m_depth = gcSearchSkill[level].m_depth;
		hi.m_nodes = gcSearchSkill[level].m_nodes;
		lo.m_name = nameLo; lo.m_terms = EVAL_ALL;
		lo.m_depth = gcSearchSkill[level-1].m_depth;
		lo.m_nodes = gcSearchSkill[level-1].m_nodes;

		runMatch(&hi, &lo, 240, verbose);
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunMatchEndgame(int verbose)
{
	// Both sides are the shipped configuration on purpose, for now.  The two
	// terms this comparison was written for were removed, and EVAL_ALL has
	// been identical to EVAL_MATERIAL|EVAL_PST ever since - it was a
	// configuration playing itself and reporting a perfect 234-234-44, which
	// looks like a result and is not one.
	//
	// Until a term exists to put on one side of it, this earns its keep as the
	// conversion baseline from thinned-out positions: level score, and the
	// share of clear material advantages that actually become wins
	// 3000 nodes less what the tables cost.  Measured on a real C64 by
	// tests/c64search.c: +9.6%, +8.5% and +8.9% at depths 2, 3 and 4, with
	// identical node counts, because that benchmark runs from the opening
	// where the blend never fires.  That is the honest figure to charge - it
	// is the overhead paid everywhere, including where the term does nothing
	t_Config on     = { "endgame tables",       EVAL_ALL, 4, 3000, 1 };
	t_Config off    = { "one set of tables",    EVAL_MATERIAL|EVAL_PST, 4, 3000, 1 };
	t_Config costed = { "tables, 2751 nodes",   EVAL_ALL, 4, 2751, 1 };

	printf("match: the endgame tables, measured in actual endgames\n");
	sc_useEndgames = 1;
	runMatch(&on, &off, 240, verbose);
	printf("match: the same at equal TIME, charged the C64's 9%%\n");
	runMatch(&costed, &off, 240, verbose);
	sc_useEndgames = 0;

	{
		t_Config openOn  = { "endgame tables, 1835 nodes", EVAL_ALL, 3, 1835, 1 };
		t_Config openOff = { "one set of tables",  EVAL_MATERIAL|EVAL_PST, 3, 2000, 1 };

		printf("match: and from openings, also at equal time\n");
		runMatch(&openOn, &openOff, 240, verbose);
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunMatchEqualTime(int verbose)
{
	t_Config rich = { "pawn struct, 1480 nodes", EVAL_ALL, 3, 1480, 1 };
	t_Config lean = { "material+pst, 2000 nodes", EVAL_MATERIAL|EVAL_PST, 3, 2000, 1 };

	printf("match: pawn structure at equal TIME rather than equal nodes\n");
	runMatch(&rich, &lean, 240, verbose);
	return 0;
}

/*-----------------------------------------------------------------------*/
int test_RunMatchTerms(int verbose)
{
	t_Config base   = { "material only",  EVAL_MATERIAL, 3, 2000, 1 };
	t_Config pst    = { "+pst",           EVAL_MATERIAL|EVAL_PST, 3, 2000, 1 };
	t_Config all    = { "everything",  EVAL_ALL, 3, 2000, 1 };

	// each term measured against what it is being added to, one at a time
	printf("match: which evaluation terms actually earn their bytes?\n");
	runMatch(&pst,   &base, 240, verbose);
	runMatch(&all,   &pst,  240, verbose);
	return 0;
}

/*-----------------------------------------------------------------------*/
// Depth is the one thing that should be unambiguously worth having.  If a
// deeper search does not beat a shallower one, something is wrong with the
// search rather than with the evaluation
int test_RunMatchDepth(int verbose)
{
	t_Config deep    = { "depth 4", EVAL_ALL, 4, 20000, 1 };
	t_Config shallow = { "depth 2", EVAL_ALL, 2, 20000, 1 };

	printf("match: does searching deeper actually win?\n");
	runMatch(&deep, &shallow, 240, verbose);
	return 0;
}

/*-----------------------------------------------------------------------*/
// What repetition detection is worth, played against its own absence.
//
// Both sides pay for the hash - it is in eng_Make either way - so this is the
// value of the draw score alone.  The equal-time half of the question is the
// second match: the detecting side gets the node budget the hash's cost
// leaves it, measured by bench against a build without the hash, so it is
// paying for what it uses rather than being handed it
int test_RunMatchRepetition(int verbose)
{
	t_Config sees   = { "sees repetitions", EVAL_ALL, 3, 2000, 1 };
	t_Config blind  = { "repeats happily",  EVAL_ALL, 3, 2000, 0 };
	// 2000 nodes less what the hash costs, and the cost is the one measured
	// on a C64 rather than on this host.  The two disagree by a lot: the host
	// says 5.5% (identical node counts, best of eleven runs), a real C64 says
	// 9.2%, 8.7% and 9.4% at depths 2, 3 and 4 by tests/c64search.c under
	// VICE.  The target's number is the one that decides whether a change is
	// affordable, so 2000 / 1.092 is the budget the hash leaves
	t_Config costed = { "sees them, 1832 nodes", EVAL_ALL, 3, 1832, 1 };

	printf("match: is scoring a repetition as a draw worth anything?\n");
	runMatch(&sees, &blind, 240, verbose);

	printf("match: the same question at equal TIME rather than equal nodes\n");
	runMatch(&costed, &blind, 240, verbose);
	return 0;
}

/*-----------------------------------------------------------------------*/
// The mate drive, played against its own absence.
//
// Measured from the endgame set as well as from the openings, because that is
// where a term gated on the phase can show anything at all - from the opening
// most games never reach a position where it fires, and the difference is
// diluted by every game that did not.
//
// There is no equal-time half to this one, and that is a measurement rather
// than an omission.  Every other term here had to be charged what it costs per
// node; this one could not be made to show a cost at all.  1500 searches of
// 60000 nodes over three endgame positions - so the term firing at every node
// of every one of them - came to 6.80s without and 6.63s with, on a host where
// the run-to-run spread is larger than the difference.  The reason is where it
// sits: outside the phase test it does not run, and inside it, it is two array
// reads and six shifts against a node that costs hundreds of cycles.
//
// **Not measured on a 6502, and that gap is real.**  The instrument for it is
// tests/c64search.c under VICE, and it benchmarks from the opening position,
// where this term cannot fire by construction - so it would faithfully report
// zero.  Charging this term honestly on target needs an endgame position added
// to that benchmark first
int test_RunMatchMateDrive(int verbose)
{
	t_Config drives = { "drives the king",  EVAL_ALL, 4, 3000, 1 };
	t_Config blind  = { "no reason to",     EVAL_ALL & ~EVAL_MATEDRIVE, 4, 3000, 1 };

	printf("match: the mate drive, from thinned-out positions\n");
	sc_useEndgames = 1;
	runMatch(&drives, &blind, 240, verbose);
	sc_useEndgames = 0;

	printf("match: the same from the openings, where most games never reach it\n");
	runMatch(&drives, &blind, 240, verbose);

	// And at level 1's budget, because Phase 9 found endgame knowledge is worth
	// most to the level that searches least, and this is the same shape of term
	{
		t_Config weakOn  = { "drives, 400 nodes", EVAL_ALL, 3, 400, 1 };
		t_Config weakOff = { "no reason, 400",    EVAL_ALL & ~EVAL_MATEDRIVE, 3, 400, 1 };

		printf("match: from endgames at level 1's budget\n");
		sc_useEndgames = 1;
		runMatch(&weakOn, &weakOff, 240, verbose);
		sc_useEndgames = 0;
	}
	return 0;
}
