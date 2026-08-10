/*
 *	uci.c
 *	cc65 Chess - test support
 *
 *	A UCI adapter, so the engine can be played against other engines by a
 *	standard match runner (fastchess, c-chess-cli, cutechess) and pointed at
 *	any GUI or analysis tool.  Native only - it uses stdio and long lines, and
 *	like everything else in tests/ it is never compiled into a cc65 target.
 *
 *	Two things about this engine make it an unusually well behaved UCI client,
 *	and both are worth knowing before setting up a match:
 *
 *	  - Strength is a node budget, not a clock.  Every clock the GUI sends is
 *	    ignored.  That makes a result exactly reproducible on any host, forever,
 *	    which is the same property the 8-bit ports rely on - so run the opponent
 *	    at fixed nodes or fixed depth too, never at a time control, or the
 *	    reproducibility is thrown away on one side of the board.
 *
 *	  - The search is deterministic.  From one position it plays one game, so a
 *	    match without a varied opening set is one game repeated N times.  Give
 *	    the runner a book.
 *
 *	Node budgets are clamped to 65535 because cc65's unsigned int is 16 bits.
 *	Asking for more natively would measure a configuration no C64 can reach.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "eval.h"
#include "search.h"
#include "cpu.h"
#include "testutil.h"

#define UCI_LINE_MAX	16384		// a 400 ply "position ... moves" line and room over
#define UCI_MAX_NODES	65535L		// what a 16 bit unsigned int can hold on the target

/*-----------------------------------------------------------------------*/
// The piece enum indexed directly: ROOK 1, KNIGHT 2, BISHOP 3, QUEEN 4
static const char sc_promoChar[] = ".rnbqkp";

/*-----------------------------------------------------------------------*/
static char s_side = SIDE_WHITE;
static char s_skill = SEARCH_NUM_SKILLS;	// 1..4, the menu's levels
static long s_optDepth;						// 0 = take it from the skill level
static long s_optNodes;						// 0 = take it from the skill level

// The opening tables in cpu.c, off by default.  Off is not timidity: the whole
// of doc/strength.md was measured through this adapter, so the default has to
// be the configuration those games were played in, and OwnBook true has to be
// something a runner asks for.  Until this existed nothing in the repository
// could reach cpu.c at all - tests/uci calls search_Best directly, and
// sargon/match.py had a copy of the white table written out in Python - so the
// tables shipped for two releases with no harness able to play them
static char s_ownBook;
static char s_bookSeed = 1;

// what cmdPosition replayed: how many moves, and white's first, as 0..63 tiles
static char s_ply;
static char s_firstFrom, s_firstTo;

/*-----------------------------------------------------------------------*/
static void say(const char *line)
{
	printf("%s\n", line);
	fflush(stdout);
}

/*-----------------------------------------------------------------------*/
// "e2e4", "e7e8q".  Writes the long algebraic form of a move
static void moveName(const t_engMove *move, char *out6)
{
	char promo = move->m_flags & ENG_MF_PROMO;

	test_TileName(ENG_TO_TILE(move->m_from), out6);
	test_TileName(ENG_TO_TILE(move->m_to), out6 + 2);
	if(promo)
	{
		out6[4] = sc_promoChar[promo];
		out6[5] = '\0';
	}
}

/*-----------------------------------------------------------------------*/
// Match a GUI's long algebraic move against the legal move list rather than
// building the move by hand.  Castling arrives as the king's e1g1 and en
// passant as a plain diagonal pawn move, so looking the move up is the only
// way to pick up the flags that make and unmake depend on
static char findMove(const char *text, t_engMove *out)
{
	t_engMove moves[ENG_MAX_MOVES];
	char count, i, from, to, promo = NONE;

	if(strlen(text) < 4)
		return 0;

	from = ENG_FROM_TILE(test_Square(text));
	to   = ENG_FROM_TILE(test_Square(text + 2));

	if(text[4])
	{
		const char *p = strchr(sc_promoChar, text[4]);
		if(!p)
			return 0;
		promo = (char)(p - sc_promoChar);
	}

	count = eng_GenLegalMoves(s_side, moves);
	for(i = 0; i < count; ++i)
	{
		if(moves[i].m_from == from && moves[i].m_to == to)
		{
			char mp = moves[i].m_flags & ENG_MF_PROMO;

			// a promotion is four different moves sharing a from and a to
			if(mp && promo != mp)
				continue;
			*out = moves[i];
			return 1;
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
// The test FEN parser stops at the en passant field, but the halfmove clock
// after it is what the fifty move rule runs on, so pick it up here
static void readHalfmove(const char *fen)
{
	const char *p = fen;
	int field = 0;

	while(*p && field < 4)
	{
		while(*p == ' ') ++p;
		while(*p && *p != ' ') ++p;
		++field;
	}
	while(*p == ' ') ++p;

	geHalfmove = (*p >= '0' && *p <= '9') ? (char)atoi(p) : 0;
}

/*-----------------------------------------------------------------------*/
static void cmdPosition(char *args)
{
	char *moves = strstr(args, "moves");

	if(moves)
	{
		*moves = '\0';
		moves += 5;
	}

	s_ply = 0;

	if(0 == strncmp(args, "startpos", 8))
	{
		eng_SetStartPosition();
		s_side = SIDE_WHITE;
	}
	else if(0 == strncmp(args, "fen ", 4))
	{
		s_side = test_EngineSetFEN(args + 4);
		readHalfmove(args + 4);
		// a game that did not start at the start position has no move one for
		// the tables to answer, so put the ply count out of their reach
		s_ply = 2;
	}
	else
		return;

	if(moves)
	{
		char *tok = strtok(moves, " \t\r\n");

		while(tok)
		{
			t_engMove move;
			t_engUndo undo;

			if(!findMove(tok, &move))
			{
				printf("info string illegal move in position command: %s\n", tok);
				fflush(stdout);
				return;
			}
			if(0 == s_ply)
			{
				s_firstFrom = ENG_TO_TILE(move.m_from);
				s_firstTo   = ENG_TO_TILE(move.m_to);
			}
			if(s_ply < 2)
				++s_ply;

			eng_Make(&move, &undo);		// never unmade - the GUI replays the whole game
			s_side = 1 - s_side;
			tok = strtok(NULL, " \t\r\n");
		}
	}
}

/*-----------------------------------------------------------------------*/
// go depth N / go nodes N beat the options, the options beat the skill level.
// Every clock the GUI sends is ignored on purpose - see the file header
static void cmdGo(char *args)
{
	t_searchResult result;
	char depth = gcSearchSkill[s_skill - 1].m_depth;
	long nodes = gcSearchSkill[s_skill - 1].m_nodes;
	char *tok = strtok(args, " \t\r\n");
	char name[8];

	if(s_optDepth) depth = (char)s_optDepth;
	if(s_optNodes) nodes = s_optNodes;

	while(tok)
	{
		char *val = strtok(NULL, " \t\r\n");

		if(!val)
			break;
		if(0 == strcmp(tok, "depth")) depth = (char)atoi(val);
		else if(0 == strcmp(tok, "nodes")) nodes = atol(val);
		tok = strtok(NULL, " \t\r\n");
	}

	if(depth < 1) depth = 1;
	if(depth > SEARCH_MAX_PLY) depth = SEARCH_MAX_PLY;
	if(nodes < 1) nodes = 1;
	if(nodes > UCI_MAX_NODES) nodes = UCI_MAX_NODES;

	// The game's own move-one path, when a runner has asked for it.  This is
	// the shipping front end's behaviour and not an approximation of it: the
	// same table, consulted by the same function, and the same randomiser
	// deciding which entry comes up
	if(s_ownBook && s_ply < 2)
	{
		search_SetSeed(s_bookSeed);
		if(cpu_BookMove(s_side, s_ply, s_firstFrom, s_firstTo, &result.m_move))
		{
			char bookName[8];

			memset(bookName, 0, sizeof(bookName));
			moveName(&result.m_move, bookName);
			printf("info depth 0 score cp 0 nodes 0 string book\n");
			printf("bestmove %s\n", bookName);
			fflush(stdout);
			return;
		}
	}

	search_Best(s_side, depth, (unsigned int)nodes, &result);

	if(!result.m_haveMove)
	{
		// mate or stalemate.  The runner works the result out from the
		// position; the null move is how UCI says "there is nothing to play"
		say("bestmove 0000");
		return;
	}

	memset(name, 0, sizeof(name));
	moveName(&result.m_move, name);

	// scores are from the side to move's view, which is what UCI wants
	if(result.m_score > EVAL_MATE - SEARCH_MAX_PLY)
		printf("info depth %d score mate %d nodes %u pv %s\n", result.m_depth,
		       (EVAL_MATE - result.m_score + 1) / 2, result.m_nodes, name);
	else if(result.m_score < -(EVAL_MATE - SEARCH_MAX_PLY))
		printf("info depth %d score mate %d nodes %u pv %s\n", result.m_depth,
		       -((EVAL_MATE + result.m_score + 1) / 2), result.m_nodes, name);
	else
		printf("info depth %d score cp %d nodes %u pv %s\n", result.m_depth,
		       result.m_score, result.m_nodes, name);

	printf("bestmove %s\n", name);
	fflush(stdout);
}

/*-----------------------------------------------------------------------*/
static void cmdSetOption(char *args)
{
	char *name = strstr(args, "name ");
	char *value = strstr(args, " value ");

	if(!name)
		return;
	name += 5;

	if(value)
	{
		*value = '\0';
		value += 7;
	}

	// trim the trailing space the split leaves on the name
	{
		size_t len = strlen(name);
		while(len && (name[len-1] == ' ' || name[len-1] == '\r' || name[len-1] == '\n'))
			name[--len] = '\0';
	}

	if(!value)
		return;

	if(0 == strcmp(name, "Skill"))
	{
		int v = atoi(value);
		if(v >= 1 && v <= SEARCH_NUM_SKILLS)
			s_skill = (char)v;
	}
	else if(0 == strcmp(name, "Depth"))
		s_optDepth = atol(value);
	else if(0 == strcmp(name, "Nodes"))
		s_optNodes = atol(value);
	else if(0 == strcmp(name, "OwnBook"))
		s_ownBook = (char)(0 == strcmp(value, "true") || atoi(value));
	else if(0 == strcmp(name, "BookSeed"))
	{
		// zero is the randomiser's dead state and means "do not randomise", so
		// it is a legal thing to ask for and reproduces the unseeded engine
		int v = atoi(value);
		if(v >= 0 && v <= 255)
			s_bookSeed = (char)v;
	}
#ifdef EVAL_TUNING
	// only the tuning build has anything to switch; a runner is free to send
	// "true"/"false" or 1/0
	else if(0 == strcmp(name, "Repetition"))
		geSearchRepetition = (char)(0 == strcmp(value, "true") || atoi(value));
	else if(0 == strcmp(name, "CheckEvasion"))
		geSearchCheckEvasion = (char)(0 == strcmp(value, "true") || atoi(value));
	else if(0 == strcmp(name, "MateDrive"))
	{
		if(0 == strcmp(value, "true") || atoi(value))
			geEvalTerms |= EVAL_MATEDRIVE;
		else
			geEvalTerms &= ~EVAL_MATEDRIVE;
	}
#endif
}

/*-----------------------------------------------------------------------*/
static void cmdUci(void)
{
	printf("id name cc65 Chess\n");
	printf("id author Stefan Wessels\n");
	// Skill is the four item menu the game itself offers.  Depth and Nodes
	// override it, which is what makes strength measurable as a curve rather
	// than as four points
	printf("option name Skill type spin default %d min 1 max %d\n",
	       SEARCH_NUM_SKILLS, SEARCH_NUM_SKILLS);
	printf("option name Depth type spin default 0 min 0 max %d\n", SEARCH_MAX_PLY);
	printf("option name Nodes type spin default 0 min 0 max %ld\n", UCI_MAX_NODES);
	// OwnBook defaults false so this adapter keeps playing the games every
	// figure in doc/strength.md was measured from.  BookSeed is which entry
	// comes up, and is the game's plat_GetSeed byte by another name
	printf("option name OwnBook type check default false\n");
	printf("option name BookSeed type spin default 1 min 0 max 255\n");
#ifdef EVAL_TUNING
	printf("option name Repetition type check default true\n");
	printf("option name CheckEvasion type check default true\n");
	printf("option name MateDrive type check default true\n");
#endif
	say("uciok");
}

/*-----------------------------------------------------------------------*/
int main(void)
{
	static char line[UCI_LINE_MAX];

	// nothing is switched here.  The shipping build has no switches at all -
	// EVAL_HAS is a constant 1 and every term is always in - and the tuning
	// build starts with everything on, so an unconfigured match plays the same
	// chess either way
	setvbuf(stdout, NULL, _IOLBF, 0);
	eng_SetStartPosition();

	while(fgets(line, sizeof(line), stdin))
	{
		char *nl = strpbrk(line, "\r\n");

		if(nl)
			*nl = '\0';

		if(0 == strcmp(line, "uci"))						cmdUci();
		else if(0 == strcmp(line, "isready"))				say("readyok");
		else if(0 == strcmp(line, "ucinewgame"))			eng_SetStartPosition();
		else if(0 == strncmp(line, "position ", 9))			cmdPosition(line + 9);
		else if(0 == strncmp(line, "setoption ", 10))		cmdSetOption(line + 10);
		else if(0 == strcmp(line, "go"))					cmdGo(line + 2);
		else if(0 == strncmp(line, "go ", 3))				cmdGo(line + 3);
		else if(0 == strcmp(line, "stop"))					/* never searching */;
		else if(0 == strcmp(line, "d"))						test_DumpBoard("position");
		else if(0 == strcmp(line, "quit"))					break;
	}
	return 0;
}
