/*
 *	mem.c
 *	cc65 Chess — Amiga
 */

#include "mem.h"

void *memset(void *s, int c, unsigned long n)
{
	unsigned char *p;

	p = (unsigned char *)s;
	while(n)
	{
		*p++ = (unsigned char)c;
		--n;
	}
	return s;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	unsigned char *dst;
	const unsigned char *src;

	dst = (unsigned char *)d;
	src = (const unsigned char *)s;
	while(n)
	{
		*dst++ = *src++;
		--n;
	}
	return d;
}

/* the body has to stay a real loop.  named strlen, gcc -Os replaces it
 * with a tail call to strlen.  -fno-builtin-strlen does not stop that;
 * -ffreestanding does.  the flag is in amiga.mk.  without it the splash's
 * first centred() never comes back. */
unsigned long strlen(const char *s)
{
	unsigned long n;

	n = 0;
	while(s[n])
		++n;
	return n;
}

/* this gcc emits a libcall for a 32-bit divide and does not ship libgcc.
 * 68000 has only 16-bit divu, so the loop below is the divide. */
unsigned long __udivsi3(unsigned long n, unsigned long d)
{
	unsigned long q, r, bit;

	q = 0;
	r = 0;
	if(!d)
		return 0;
	for(bit = 1UL << 31; bit; bit >>= 1)
	{
		r <<= 1;
		if(n & bit)
			r |= 1;
		if(r >= d)
		{
			r -= d;
			q |= bit;
		}
	}
	return q;
}

long __divsi3(long a, long b)
{
	unsigned long ua, ub;
	int neg;

	neg = 0;
	if(a < 0)
	{
		neg = 1;
		ua = (unsigned long)(-a);
	}
	else
		ua = (unsigned long)a;
	if(b < 0)
	{
		neg = !neg;
		ub = (unsigned long)(-b);
	}
	else
		ub = (unsigned long)b;
	if(neg)
		return -(long)__udivsi3(ua, ub);
	return (long)__udivsi3(ua, ub);
}
