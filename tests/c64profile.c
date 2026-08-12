/*
 *	c64profile.c
 *	cc65 Chess - retained in-situ C64 search component profiler
 *
 *	Each row asks the shipping search to do one extra, redundant copy of a
 *	component's work.  The difference from PROFILE_NONE is that component's
 *	cost including cc65's call and indexing overhead.  Baseline and candidate
 *	passes alternate order over two rounds, and every pass replays the same
 *	middlegame positions.  A changed result digest or node count is a failed
 *	measurement, not a timing result.
 *
 *	Build at the shipping optimisation setting:
 *	  cl65 -t c64 -Or -I../src -I. -DSEARCH_PROFILE -o c64profile.prg \
 *	       ../src/engine.c ../src/eval.c ../src/search.c c64profile.c
 */

#include <stdio.h>
#ifndef PROFILE_EXTERNAL_CLOCK
#include <time.h>
#endif
#include "types.h"
#include "engine.h"
#include "search.h"
#include "c64profile.h"

// The fixed level-2 game used by c64evasion.c, stored as 0x88 from/to pairs.
// Search starts after development and stops before the late ending, so this
// prices the middlegame rather than the unusually cheap opening.
static const char sc_game[] =
{
	0x71,0x52, 0x01,0x22, 0x76,0x55, 0x06,0x25, 0x63,0x43, 0x13,0x33,
	0x64,0x54, 0x02,0x24, 0x75,0x53, 0x03,0x23, 0x74,0x76, 0x04,0x02,
	0x72,0x63, 0x02,0x01, 0x73,0x64, 0x03,0x04, 0x70,0x73, 0x04,0x03,
	0x75,0x74, 0x03,0x04, 0x60,0x50, 0x07,0x06, 0x67,0x57, 0x06,0x07,
	0x50,0x40, 0x04,0x03, 0x53,0x31, 0x25,0x44, 0x52,0x44, 0x33,0x44,
	0x31,0x22, 0x23,0x22, 0x55,0x36, 0x22,0x62, 0x36,0x24, 0x15,0x24,
	0x64,0x31, 0x62,0x53, 0x63,0x52, 0x53,0x31,
};

#define GAME_PLIES	40
#define FIRST_SEARCH	16
#define LAST_SEARCH	22
#define PROFILE_ROUNDS	2

// Candidate work can rebuild only the affected row instead of paying for the
// full matrix.  The default remains every component.
#ifndef PROFILE_FIRST_COMPONENT
#define PROFILE_FIRST_COMPONENT	1
#endif
#ifndef PROFILE_LAST_COMPONENT
#define PROFILE_LAST_COMPONENT	PROFILE_COMPONENTS
#endif

static t_engMove st_moves[80];
static unsigned long sl_jiffies;
static unsigned long sl_nodes;
static unsigned int su_digest;

#ifdef PROFILE_EXTERNAL_CLOCK
// The Apple II profiler has no cc65 clock().  It waits outside the measured
// search at each marker while a2m-v2 pauses, reads its exact emulated cycle
// counter, clears the acknowledgement byte and resumes.
volatile char gc_profileMarker;
volatile char gc_profileAck;
#endif

static unsigned long sl_baseTime[PROFILE_COMPONENTS + 1];
static unsigned long sl_profileTime[PROFILE_COMPONENTS + 1];
static unsigned long sl_baseNodes[PROFILE_COMPONENTS + 1];
static unsigned long sl_profileNodes[PROFILE_COMPONENTS + 1];
static unsigned int su_baseDigest[PROFILE_COMPONENTS + 1];
static unsigned int su_profileDigest[PROFILE_COMPONENTS + 1];

#ifdef PROFILE_EXTERNAL_CLOCK
/*-----------------------------------------------------------------------*/
static void profileMark(char marker)
{
	gc_profileMarker = marker;
	gc_profileAck = 1;
	while(gc_profileAck)
		;
}
#endif

/*-----------------------------------------------------------------------*/
static void digestResult(const t_searchResult *result)
{
	// A rotate and xor is enough here: this is an identity tripwire, not a
	// position key, and its inputs include every observable search result.
	su_digest = (su_digest << 5) | (su_digest >> 11);
	su_digest ^= result->m_move.m_from;
	su_digest ^= (unsigned int)result->m_move.m_to << 8;
	su_digest ^= result->m_move.m_flags;
	su_digest ^= result->m_score;
	su_digest ^= (unsigned int)result->m_depth << 8;
	su_digest ^= result->m_nodes;
}

/*-----------------------------------------------------------------------*/
static char playScripted(char ply, char side, t_engUndo *undo)
{
	char count, i;
	char from = sc_game[ply << 1];
	char to = sc_game[(ply << 1) + 1];

	count = eng_GenMoves(side, st_moves, 80);
	for(i = 0; i < count; ++i)
		if(st_moves[i].m_from == from && st_moves[i].m_to == to)
		{
			eng_Make(&st_moves[i], undo);
			return 1;
		}

	return 0;
}

/*-----------------------------------------------------------------------*/
static char runPass(char component)
{
	t_engUndo undo;
	char side = SIDE_WHITE, ply;

	sl_jiffies = 0;
	sl_nodes = 0;
	su_digest = 0;
	geSearchProfile = PROFILE_NONE;
	search_SetSeed(0);
	eng_SetStartPosition();

	for(ply = 0; ply < GAME_PLIES; ++ply)
	{
		if(ply >= FIRST_SEARCH && ply < LAST_SEARCH)
		{
			t_searchResult result;
#ifndef PROFILE_EXTERNAL_CLOCK
			clock_t start;
#endif

#ifdef PROFILE_EXTERNAL_CLOCK
			if(FIRST_SEARCH == ply)
				profileMark(0x80 | component);
#endif
			geSearchProfile = component;
#ifndef PROFILE_EXTERNAL_CLOCK
			start = clock();
#endif
			search_Best(side, gcSearchSkill[1].m_depth,
			            gcSearchSkill[1].m_nodes, &result);
#ifndef PROFILE_EXTERNAL_CLOCK
			sl_jiffies += (unsigned long)(clock() - start);
#endif
			geSearchProfile = PROFILE_NONE;
			sl_nodes += result.m_nodes;
			digestResult(&result);
#ifdef PROFILE_EXTERNAL_CLOCK
			if(LAST_SEARCH - 1 == ply)
				profileMark(component);
#endif
		}

		if(!playScripted(ply, side, &undo))
			return 0;
		side = 1 - side;
	}

	geSearchProfile = PROFILE_NONE;
	return 1;
}

/*-----------------------------------------------------------------------*/
static char recordPass(char component, char profiled)
{
	if(!runPass(profiled ? component : PROFILE_NONE))
		return 0;

	if(profiled)
	{
		sl_profileTime[component] += sl_jiffies;
		sl_profileNodes[component] += sl_nodes;
		su_profileDigest[component] += su_digest;
	}
	else
	{
		sl_baseTime[component] += sl_jiffies;
		sl_baseNodes[component] += sl_nodes;
		su_baseDigest[component] += su_digest;
	}

	return 1;
}

/*-----------------------------------------------------------------------*/
int main(void)
{
	char component, round;
#ifdef PROFILE_EXTERNAL_CLOCK
	char allSame = 1;
#endif

	printf("cc65 chess current profile\n");
	printf("positions %u-%u rounds %u\n",
	       (unsigned)FIRST_SEARCH, (unsigned)(LAST_SEARCH - 1),
	       (unsigned)PROFILE_ROUNDS);

	for(round = 0; round < PROFILE_ROUNDS; ++round)
		for(component = PROFILE_FIRST_COMPONENT;
		    component <= PROFILE_LAST_COMPONENT; ++component)
		{
			char ok;

			putchar('.');
			if(PROFILE_LAST_COMPONENT == component)
				putchar('\n');

			// Alternate the order so emulator warm-up or host load cannot all
			// lean in the same direction.
			if(round & 1)
			{
				ok = recordPass(component, 1);
				ok = ok && recordPass(component, 0);
			}
			else
			{
				ok = recordPass(component, 0);
				ok = ok && recordPass(component, 1);
			}
			if(!ok)
			{
				printf("desync c=%u r=%u\n", (unsigned)component,
				       (unsigned)round);
				for(;;)
					;
			}
		}

	for(component = PROFILE_FIRST_COMPONENT;
	    component <= PROFILE_LAST_COMPONENT; ++component)
	{
		char same;

		same = sl_baseNodes[component] == sl_profileNodes[component] &&
		       su_baseDigest[component] == su_profileDigest[component];
#ifdef PROFILE_EXTERNAL_CLOCK
		if(!same)
			allSame = 0;
#endif
		printf("%02u n=%lu b=%lu p=%lu %s\n", (unsigned)component,
		       sl_baseNodes[component], sl_baseTime[component],
		       sl_profileTime[component], same ? "ok" : "BAD");
	}

	printf("done.\n");

#ifdef PROFILE_EXTERNAL_CLOCK
	profileMark(allSame ? 0x7F : 0xFF);
#endif

	for(;;)
		;
}
