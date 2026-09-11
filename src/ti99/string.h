/*
 *	string.h
 *	cc65 Chess — TI-99/4A
 *
 *	tms9900-gcc has no libc.  engine.c includes this for memset.
 */

#ifndef _STRING_H_
#define _STRING_H_

void *memset(void *s, int c, unsigned int n);
void *memcpy(void *d, const void *s, unsigned int n);
unsigned int strlen(const char *s);

#endif
