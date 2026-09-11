* Bank 0 of a paged378 cart.  copies the EA5 image from banks 1+ into
* 32K RAM and jumps to >A000.  sizes after the CHSS marker are patched
* by make/ti99-mkcart.py.
*
* paged378 bank select: write to >6000 + bank*2.

	pseg
	even

	byte	>AA, >01
	data	>0000
	data	>0000, proglist
	data	>0000, >0000

proglist:
	data	>0000
	data	_start
	byte	5
	text	'CHESS'
	even

	text	'CHSS'
	even
payload_info:
text_dest:
	data	>A000
text_size:
	data	>0000
data_dest:
	data	>2000
data_size:
	data	>0000

	def	_start
_start:
	limi	0
	lwpi	>8300
	mov	@text_dest, r9
	mov	@text_size, r7
	mov	@data_dest, r4
	mov	@data_size, r5
	li	r0, copier
	li	r1, >8340
	li	r2, copier_end
hdr_copy:
	c	r0, r2
	jhe	hdr_go
	mov	*r0+, *r1+
	jmp	hdr_copy
hdr_go:
	b	@>8340

* runs from scratchpad.  only PC-relative jumps — the cart window is
* payload once we start banking.
copier:
	li	r8, 1
	clr	r6
	clr	r12
do_region:
	mov	r7, r7
	jeq	region_done
copy_loop:
	mov	r7, r7
	jeq	region_done
	mov	r8, r0
	sla	r0, 1
	ai	r0, >6000
	clr	*r0
	li	r1, >6000
	a	r6, r1
	li	r2, >2000
	s	r6, r2
	c	r2, r7
	jl	got_n
	mov	r7, r2
got_n:
	mov	r2, r3
word_copy:
	mov	*r1+, *r9+
	dect	r3
	jgt	word_copy
	s	r2, r7
	a	r2, r6
	ci	r6, >2000
	jne	copy_loop
	clr	r6
	inc	r8
	jmp	copy_loop
region_done:
	inc	r12
	ci	r12, 1
	jne	go_ram
	mov	r4, r9
	mov	r5, r7
	jmp	do_region
go_ram:
	b	@>A000
copier_end:
	even
