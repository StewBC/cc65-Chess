/*
 *	mem.h
 *	cc65 Chess — Amiga
 *
 *	The SDK string.h declares memset and memcpy and does not supply them.
 *	strlen is not declared there at all.  These three are the whole libc
 *	this port needs, and they live here so the GPL startup that ships with
 *	the compiler stays out of this tree.
 */

#ifndef _AMIGA_MEM_H_
#define _AMIGA_MEM_H_

void *memset(void *s, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
unsigned long strlen(const char *s);

#endif
