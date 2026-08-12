/*
 *	collectpos.c
 *	cc65 Chess - test support
 *
 *	Play the shipping search from book.epd openings and dump every root
 *	position the engine actually reaches, labelled by the game result.
 *
 *	Openings are split by index: even = train, odd = locked val.  A position
 *	from one deterministic game cannot appear in both.  The landing gauntlet
 *	is not this file's business.
 *
 *	Built like tests/uci, without EVAL_TUNING, so the eval is what ships.
 *
 *	  ./collectpos --split train --nodes 1200 --depth 4 --out train.tsv
 *	  ./collectpos --eval book.epd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "globals.h"
#include "engine.h"
#include "eval.h"
#include "search.h"
#include "board.h"
#include "undo.h"
#include "testutil.h"

#define MAX_BOOK		256
#define MAX_PLIES		240
#define FEN_BYTES		96

typedef struct tag_Snap
{
	char	m_fen[FEN_BYTES];
	int		m_eval;
	int		m_ply;
	char	m_stm;
} t_Snap;

static char st_book[MAX_BOOK][FEN_BYTES];
static int st_nbook;

/*-----------------------------------------------------------------------*/
static int loadBook(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[256];

	if(!fp)
	{
		fprintf(stderr, "collectpos: cannot open %s\n", path);
		return 0;
	}
	st_nbook = 0;
	while(st_nbook < MAX_BOOK && fgets(line, sizeof(line), fp))
	{
		char *nl = strpbrk(line, "\r\n");
		if(nl)
			*nl = '\0';
		if(!line[0] || line[0] == '#')
			continue;
		strncpy(st_book[st_nbook], line, FEN_BYTES - 1);
		st_book[st_nbook][FEN_BYTES - 1] = '\0';
		++st_nbook;
	}
	fclose(fp);
	return 1;
}

/*-----------------------------------------------------------------------*/
static int playGame(const char *fen, int depth, unsigned int nodes,
                    t_Snap *snaps, int *nSnaps)
{
	char side = test_EngineSetFEN(fen);
	int ply;

	undo_Init();
	board_SyncDisplay();
	*nSnaps = 0;

	for(ply = 0; ply < MAX_PLIES; ++ply)
	{
		t_searchResult result;
		char outcome = search_Outcome(side);

		if(OUTCOME_CHECKMATE == outcome)
			return (side == SIDE_WHITE) ? 0 : 2;
		if(OUTCOME_STALEMATE == outcome)
			return 1;
		if(geHalfmove >= 100)
			return 1;
		if(eng_IsRepetition(2))
			return 1;

		if(*nSnaps < MAX_PLIES)
		{
			test_EngineGetFEN(side, snaps[*nSnaps].m_fen);
			snaps[*nSnaps].m_eval = eval_Position(SIDE_WHITE);
			snaps[*nSnaps].m_ply = ply;
			snaps[*nSnaps].m_stm = side;
			++*nSnaps;
		}

		search_Best(side, (char)depth, (unsigned int)nodes, &result);
		if(!result.m_haveMove)
			return 1;

		board_ApplyMove(&result.m_move, side);
		side = 1 - side;
	}
	return 1;
}

/*-----------------------------------------------------------------------*/
static int wantOpening(int index, const char *split)
{
	if(0 == strcmp(split, "all"))
		return 1;
	if(0 == strcmp(split, "train"))
		return (index % 2) == 0;
	if(0 == strcmp(split, "val"))
		return (index % 2) == 1;
	return 0;
}

/*-----------------------------------------------------------------------*/
static void usage(void)
{
	fprintf(stderr,
		"usage: collectpos [--book FILE] [--split train|val|all]\n"
		"                  [--nodes N] [--depth D] [--out FILE]\n"
		"       collectpos --eval FILE\n");
}

/*-----------------------------------------------------------------------*/
int main(int argc, char **argv)
{
	const char *book = "book.epd";
	const char *split = "train";
	const char *outPath = 0;
	int depth = 4;
	int nodes = 1200;
	int evalOnly = 0;
	const char *evalFile = 0;
	int i, nGames, nPos;
	FILE *out;
	t_Snap snaps[MAX_PLIES];

	for(i = 1; i < argc; ++i)
	{
		if(0 == strcmp(argv[i], "--book") && i + 1 < argc)
			book = argv[++i];
		else if(0 == strcmp(argv[i], "--split") && i + 1 < argc)
			split = argv[++i];
		else if(0 == strcmp(argv[i], "--nodes") && i + 1 < argc)
			nodes = atoi(argv[++i]);
		else if(0 == strcmp(argv[i], "--depth") && i + 1 < argc)
			depth = atoi(argv[++i]);
		else if(0 == strcmp(argv[i], "--out") && i + 1 < argc)
			outPath = argv[++i];
		else if(0 == strcmp(argv[i], "--eval") && i + 1 < argc)
		{
			evalOnly = 1;
			evalFile = argv[++i];
		}
		else if(0 == strcmp(argv[i], "-h") || 0 == strcmp(argv[i], "--help"))
		{
			usage();
			return 0;
		}
		else
		{
			usage();
			return 2;
		}
	}

	if(evalOnly)
	{
		FILE *fp = fopen(evalFile, "r");
		char line[256];

		if(!fp)
		{
			fprintf(stderr, "collectpos: cannot open %s\n", evalFile);
			return 1;
		}
		while(fgets(line, sizeof(line), fp))
		{
			char *nl = strpbrk(line, "\r\n");
			char side;

			if(nl)
				*nl = '\0';
			if(!line[0] || line[0] == '#')
				continue;
			side = test_EngineSetFEN(line);
			printf("%d %c %s\n", eval_Position(SIDE_WHITE),
			       side == SIDE_WHITE ? 'w' : 'b', line);
		}
		fclose(fp);
		return 0;
	}

	if(0 == strcmp(split, "train") || 0 == strcmp(split, "val") ||
	   0 == strcmp(split, "all"))
		;
	else
	{
		fprintf(stderr, "collectpos: --split must be train, val or all\n");
		return 2;
	}

	if(!loadBook(book))
		return 1;

	out = outPath ? fopen(outPath, "w") : stdout;
	if(!out)
	{
		fprintf(stderr, "collectpos: cannot write %s\n", outPath);
		return 1;
	}

	fprintf(out, "# split=%s nodes=%d depth=%d book=%s nbook=%d\n",
	        split, nodes, depth, book, st_nbook);
	fprintf(out, "opening\tskill_nodes\tply\tstm\tresult\teval\tfen\n");

	nGames = 0;
	nPos = 0;
	for(i = 0; i < st_nbook; ++i)
	{
		int nSnaps, result, s;

		if(!wantOpening(i, split))
			continue;

		result = playGame(st_book[i], depth, (unsigned int)nodes, snaps, &nSnaps);
		++nGames;
		for(s = 0; s < nSnaps; ++s)
		{
			fprintf(out, "%d\t%d\t%d\t%c\t%s\t%d\t%s\n",
			        i, nodes, snaps[s].m_ply,
			        snaps[s].m_stm == SIDE_WHITE ? 'w' : 'b',
			        result == 2 ? "1" : result == 0 ? "0" : "0.5",
			        snaps[s].m_eval, snaps[s].m_fen);
			++nPos;
		}
		fprintf(stderr, "  opening %d  result %s  %d positions\n",
		        i, result == 2 ? "1-0" : result == 0 ? "0-1" : "1/2", nSnaps);
	}

	fprintf(stderr, "collectpos: %d games, %d positions (%s)\n", nGames, nPos, split);
	if(outPath)
		fclose(out);
	return 0;
}
