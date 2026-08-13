/*
 *	movecache.c
 *	cc65 Chess - host instrument for F4
 *
 *	Not compiled into any 8-bit target.  Build three times:
 *
 *	  make movecache32 movecache64 movecache128
 *
 *	Runs every book.epd position at the four shipped budgets and prints
 *	probe stats plus nodes and completed-depth sums.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "engine.h"
#include "search.h"
#include "eval.h"
#include "testutil.h"

#ifndef SEARCH_MOVE_CACHE
#error "movecache.c is the F4 host instrument; build with -DSEARCH_MOVE_CACHE=32|64|128"
#endif

#define MAX_BOOK	256
#define FEN_BYTES	96

static char st_book[MAX_BOOK][FEN_BYTES];
static int st_nbook;

/*-----------------------------------------------------------------------*/
static int loadBook(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[256];

	if(!fp)
	{
		fprintf(stderr, "movecache: cannot open %s\n", path);
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
int main(int argc, char **argv)
{
	const char *book = "book.epd";
	int level, i;
	unsigned long probes, occupied, locks, found, useful;

	if(argc > 1)
		book = argv[1];
	if(!loadBook(book))
		return 1;

	printf("F4 move cache: %d entries, %d positions\n",
	       SEARCH_MOVE_CACHE, st_nbook);
	printf("level     nodes   depth  probes   occup    lock   found  useful\n");

	search_MoveCacheReset();
	for(level = 0; level < SEARCH_NUM_SKILLS; ++level)
	{
		unsigned long nodes = 0;
		int depthsum = 0;
		unsigned long p0, o0, l0, f0, u0;

		search_MoveCacheStats(&p0, &o0, &l0, &f0, &u0);
		for(i = 0; i < st_nbook; ++i)
		{
			t_searchResult result;
			char side = test_EngineSetFEN(st_book[i]);

			search_Best(side, gcSearchSkill[level].m_depth,
			            gcSearchSkill[level].m_nodes, &result);
			nodes += result.m_nodes;
			depthsum += result.m_depth;
		}
		search_MoveCacheStats(&probes, &occupied, &locks, &found, &useful);
		printf("%5d %9lu %7d %7lu %7lu %7lu %7lu %7lu\n",
		       level + 1, nodes, depthsum,
		       probes - p0, occupied - o0, locks - l0, found - f0, useful - u0);
	}

	search_MoveCacheStats(&probes, &occupied, &locks, &found, &useful);
	printf("all   %7lu %7lu %7lu %7lu %7lu  (cumulative)\n",
	       probes, occupied, locks, found, useful);
	return 0;
}
