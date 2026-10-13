/*
 *	startup.c
 *	cc65 Chess — Amiga
 *
 *	Opens the three libraries the screen needs, calls main, and puts the
 *	task's stack bounds back so the rts in start.s returns on the CLI stack.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "mem.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

extern char stack_base[];
extern char stack_top[];
extern int main(void);
extern void amiga_exit(void);

static APTR sc_oldLower, sc_oldUpper;

static void say(const char *s)
{
	BPTR out;

	if(!DOSBase)
		return;
	out = Output();
	if(out)
		Write(out, (APTR)s, (LONG)strlen(s));
}

static void close_libs(void)
{
	if(IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	if(GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if(DOSBase)
		CloseLibrary((struct Library *)DOSBase);
	IntuitionBase = 0;
	GfxBase = 0;
	DOSBase = 0;
}

void amiga_fail(const char *s)
{
	say(s);
	close_libs();
	if(SysBase)
	{
		SysBase->ThisTask->tc_SPLower = sc_oldLower;
		SysBase->ThisTask->tc_SPUpper = sc_oldUpper;
	}
	amiga_exit();
}

int amiga_main(void)
{
	int rc;

	SysBase = *(struct ExecBase *volatile *)4UL;
	sc_oldLower = SysBase->ThisTask->tc_SPLower;
	sc_oldUpper = SysBase->ThisTask->tc_SPUpper;
	SysBase->ThisTask->tc_SPLower = stack_base;
	SysBase->ThisTask->tc_SPUpper = stack_top;

	DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 0);
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
	IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 0);
	if(!DOSBase || !GfxBase || !IntuitionBase)
		amiga_fail("Chess: Kickstart libraries did not open.\n");

	rc = main();
	close_libs();
	SysBase->ThisTask->tc_SPLower = sc_oldLower;
	SysBase->ThisTask->tc_SPUpper = sc_oldUpper;
	return rc;
}
