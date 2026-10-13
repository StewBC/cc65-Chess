| start.s
| cc65 Chess — Amiga
|
| DOS calls a CLI program with a jsr and a few kilobytes of stack.  The
| search is only twelve plies deep, but a gcc frame is fatter than a cc65
| one, so the first thing this does is move onto a stack of our own.  The
| return address and the original pointer are put back before rts: DOS
| unloads this seglist on the way out, and that seglist holds the stack.

	| DOS ignores the ELF entry and runs the first instruction of the
	| first hunk.  gcc puts main in .text.startup.* and the linker script
	| places that ahead of .text, so the entry has to be .text.startup
	| itself or the CLI walks into main with the libraries still shut.
	.section .text.startup,"ax"
	.globl	_start
_start:
	move.l	(%sp)+,%d0
	move.l	%d0,dos_return
	move.l	%sp,dos_sp
	lea	stack_top,%sp
	jsr	amiga_main
	moveq	#0,%d0
	bra	leave

	.globl	amiga_exit
amiga_exit:
	moveq	#20,%d0
leave:
	move.l	dos_sp,%sp
	move.l	dos_return,-(%sp)
	rts

	.bss
	.align	2
dos_return:
	.skip	4
dos_sp:
	.skip	4
	.align	2
	.globl	stack_base
stack_base:
	.skip	32768
	.globl	stack_top
stack_top:
