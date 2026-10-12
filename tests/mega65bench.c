/* identical middlegames and real skill budgets on MEGA65, C64, ARM2 and host.
 * native/C64 ticks use cascaded CIA2 timers; test-only ownership.
 * milliseconds assume 1 MHz; correct for the NTSC C64 CIA clock.
 * sums are milliseconds so level 4 cannot wrap a 32-bit microsecond total.
 * ARCHIE_BENCH uses RISC OS centiseconds instead of CIA timers.
 * records stay in RAM for the emulator monitor.  no shipping engine changes. */
#include "types.h"
#include "engine.h"
#include "search.h"
#ifdef ARCHIE_BENCH
#include <kernel.h>
#include <swis.h>
#endif
#if defined(__MEGA65__)
#include <mega65.h>
#elif !defined(__CC65__)
#include <stdio.h>
#endif

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


typedef struct {
	unsigned long nodes, millis;
	unsigned short digest;
} t_bench;
volatile t_bench bench[4];
volatile char benchDone;
int plat_ReadKeys(char blocking) { (void)blocking; return 0; }
static t_engMove moves[80];

#if defined(__MEGA65__) || defined(__CC65__)
#define TIMER ((volatile char *)0xdd00)
static unsigned long ticks(void)
{
	unsigned int hi, hi2, lo;
	do {
		hi = TIMER[6] | ((unsigned int)TIMER[7] << 8);
		lo = TIMER[4] | ((unsigned int)TIMER[5] << 8);
		hi2 = TIMER[6] | ((unsigned int)TIMER[7] << 8);
	} while(hi != hi2);
	return ~(((unsigned long)hi << 16) | lo);
}
#elif defined(ARCHIE_BENCH)
static unsigned long ticks(void)
{
	_kernel_swi_regs r;
	if(_kernel_swi(OS_ReadMonotonicTime, &r, &r)) return 0;
	return (unsigned long)r.r[0] * 10000UL;
}
#else
static unsigned long ticks(void) { return 0; }
#endif

int main(void)
{
	char level, ply, side, i, count;
	t_engUndo undo;
	t_searchResult result;
	unsigned long start;
	unsigned short digest;
#if defined(__MEGA65__)
	VICIV.key = 0x47;
	VICIV.key = 0x53;
	VICIV.ctrlc |= VIC4_VFAST_MASK;
#endif
#if defined(__MEGA65__) || defined(__CC65__)
	TIMER[14] = TIMER[15] = 0;
	TIMER[4] = TIMER[5] = TIMER[6] = TIMER[7] = 255;
	TIMER[15] = 0x51;
	TIMER[14] = 0x11;
#endif
	for(level = 0; level < 4; ++level)
	{
		eng_SetStartPosition();
		search_SetSeed(0);
		side = SIDE_WHITE;
		for(ply = 0; ply < 22; ++ply)
		{
			if(ply >= 16)
			{
				start = ticks();
				search_Best(side, gcSearchSkill[level].m_depth,
					gcSearchSkill[level].m_nodes, &result);
				bench[level].millis += (ticks() - start) / 1000;
				bench[level].nodes += result.m_nodes;
				digest = bench[level].digest;
				digest = (digest << 5) | (digest >> 11);
				bench[level].digest = digest ^ result.m_nodes ^
					(unsigned int)result.m_score ^
					((unsigned int)result.m_move.m_from << 8) ^ result.m_move.m_to ^
					result.m_move.m_flags ^ result.m_depth;
			}
			count = eng_GenMoves(side, moves, 80);
			for(i = 0; i < count; ++i)
				if(moves[i].m_from == sc_game[ply * 2] &&
					moves[i].m_to == sc_game[ply * 2 + 1]) break;
			if(i == count) { benchDone = 255; return 1; }
			eng_Make(&moves[i], &undo);
			side = 1 - side;
		}
#if !defined(__MEGA65__) && !defined(__CC65__)
		printf("level %u nodes %lu ms %lu digest %04x\r\n", level + 1,
			bench[level].nodes, bench[level].millis, bench[level].digest);
#endif
	}
	benchDone = 1;
#if defined(__MEGA65__) || defined(__CC65__)
	for(;;) {}
#else
	return 0;
#endif
}
