* EA5 / RAM crt0.  cart loader copies .text to >A000 and .data to >2000
* before jumping here.  both stack pointers — R10 still frames locals on
* this gcc, R15 is what the patch notes moved SP to.

	pseg
	even
	def	_start
	ref	main
	ref	_bss_start
	ref	_bss_end

_start:
	limi	0
	lwpi	>8300
	li	r10, >FFFC
	li	r15, >FFFC

	li	r1, _bss_start
	li	r2, _bss_end
bss_loop:
	c	r1, r2
	jhe	bss_done
	clr	*r1+
	jmp	bss_loop
bss_done:
	bl	@main
	blwp	@>0000
