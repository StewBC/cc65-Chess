| c2p.s
| cc65 Chess — Amiga
|
| Chunky bytes are two pixels, low nibble on the left.  Four of those
| bytes become one byte on each of the four separate planes.  The tables
| are built in platAmiga.c: one long per source byte, already shifted to
| the bit pair that byte owns, plane 0 in the high byte.
|
| 68000 indexed mode only has an 8-bit displacement, so the four tables
| (1024 bytes apart) each get a base register.  1024(%a2,%d0) would be a
| 68020 instruction and an A500 would guru on it.

	.section .text
	.globl	c2p_span
c2p_span:
	movem.l	%d2-%d7/%a2-%a5,-(%sp)
	move.l	44(%sp),%a0
	move.l	48(%sp),%a1
	move.l	52(%sp),%d2
	move.l	56(%sp),%d3
	move.l	60(%sp),%a2
	tst.w	%d2
	beq	c2p_done
	tst.w	%d3
	beq	c2p_done
	subq.w	#1,%d2
	subq.w	#1,%d3
	move.l	%a2,%a3
	adda.w	#1024,%a3
	move.l	%a2,%a4
	adda.w	#2048,%a4
	move.l	%a2,%a5
	adda.w	#3072,%a5
c2p_row:
	move.w	%d2,%d5
	move.l	%a0,%d6
	move.l	%a1,%d7
c2p_col:
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	lsl.w	#2,%d0
	move.l	(%a2,%d0.w),%d4
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	lsl.w	#2,%d0
	or.l	(%a3,%d0.w),%d4
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	lsl.w	#2,%d0
	or.l	(%a4,%d0.w),%d4
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	lsl.w	#2,%d0
	or.l	(%a5,%d0.w),%d4
	| plane 3 is the low byte.  30720 fits in a 16-bit displacement.
	move.b	%d4,30720(%a1)
	lsr.l	#8,%d4
	move.b	%d4,20480(%a1)
	lsr.l	#8,%d4
	move.b	%d4,10240(%a1)
	lsr.l	#8,%d4
	move.b	%d4,(%a1)+
	dbra	%d5,c2p_col
	move.l	%d6,%a0
	adda.w	#160,%a0
	move.l	%d7,%a1
	adda.w	#40,%a1
	dbra	%d3,c2p_row
c2p_done:
	movem.l	(%sp)+,%d2-%d7/%a2-%a5
	rts
