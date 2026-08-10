/*
 *	cpu.h
 *	cc65 Chess
 *
 *	Created by Stefan Wessels, February 2014.
 *
 */

#ifndef _CPU_H_
#define _CPU_H_

#include "engine.h"

char cpu_Play(char side);

// The opening tables.  ply is how many moves have been played - only 0 and 1
// are ever answered - and wFrom/wTo are white's first move, as 0..63 tiles,
// when ply is 1.  Returns 1 and fills move when a table has an answer.
//
// Exported for tests/uci: without it no harness in the repository could reach
// this code at all, so the tables shipped for two releases with nothing but
// tests/opening.c able to see them
char cpu_BookMove(char side, char ply, char wFrom, char wTo, t_engMove *move);

#endif //_CPU_H_