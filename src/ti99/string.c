/*
 *	string.c
 *	cc65 Chess — TI-99/4A
 */

#include "string.h"

void *memset(void *s, int c, unsigned int n)
{
	char *p;

	p = (char *)s;
	while(n)
	{
		*p++ = (char)c;
		--n;
	}
	return s;
}

void *memcpy(void *d, const void *s, unsigned int n)
{
	char *dst;
	const char *src;

	dst = (char *)d;
	src = (const char *)s;
	while(n)
	{
		*dst++ = *src++;
		--n;
	}
	return d;
}

unsigned int strlen(const char *s)
{
	unsigned int n;

	n = 0;
	while(*s++)
		++n;
	return n;
}
