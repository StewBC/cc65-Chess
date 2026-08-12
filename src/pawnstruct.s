;
; pawnstruct.s
; cc65 Chess
;
; Size-conscious doubled / isolated score for the 6502 ports.  Host / term
; use the C reference in eval.c.  Doses: doubled -8, isolated -16.
;
; int pawnStructScore(void) — white-positive structure score in AX
;
; Build with ca65 -DEVAL_PAWNSTRUCT_ON=0 for a stub.  Default is on.
;

	.export		_pawnStructScore
	.import		_geBoard

.ifndef EVAL_PAWNSTRUCT_ON
	EVAL_PAWNSTRUCT_ON = 1
.endif

.if EVAL_PAWNSTRUCT_ON

	.bss
sc_count:	.res	16
base:		.res	1
file:		.res	1
n:		.res	1
pen:		.res	2
score:		.res	2

	.code

.proc	_pawnStructScore: near

	; --- clear counts ---
	ldx	#15
	lda	#0
:	sta	sc_count,x
	dex
	bpl	:-

	; --- count pawns ---
	ldx	#0
walk:
	txa
	and	#$88
	bne	wnext
	lda	_geBoard,x
	tay
	and	#7
	cmp	#6
	bne	wnext
	tya
	and	#$80
	beq	:+
	lda	#8
	.byte	$2C			; BIT abs: skip next two-byte lda
:	lda	#0
	sta	base
	txa
	and	#7
	ora	base
	tay
	lda	sc_count,y
	clc
	adc	#1
	sta	sc_count,y
wnext:
	inx
	cpx	#$78
	bcc	walk

	; --- score both sides ---
	lda	#0
	sta	score
	sta	score+1
	sta	base			; 0 = black, 8 = white

sideloop:
	lda	#0
	sta	pen
	sta	pen+1
	sta	file

fileloop:
	lda	base
	ora	file
	tay
	lda	sc_count,y
	beq	nextf
	sta	n

	; doubled: (n-1)*8 when n>=2
	cmp	#2
	bcc	iso
	sbc	#1			; C=1 from cmp
	asl	a
	asl	a
	asl	a			; *8, C=0
	adc	pen
	sta	pen
	; penH stays 0: max doubled pen is 7*8=56
iso:
	; isolated?
	lda	file
	beq	chkright		; a-file: no left
	lda	base
	ora	file
	tay
	dey
	lda	sc_count,y
	bne	nextf
chkright:
	lda	file
	cmp	#7
	beq	doiso			; h-file: no right
	lda	base
	ora	file
	tay
	iny
	lda	sc_count,y
	bne	nextf
doiso:
	lda	n
	asl	a
	asl	a
	asl	a
	asl	a			; *16
	adc	pen			; C=0 from asls
	sta	pen
	bcc	nextf
	inc	pen+1

nextf:
	inc	file
	lda	file
	cmp	#8
	bcc	fileloop

	; black (base=0): score += pen; white (base=8): score -= pen
	lda	base
	bne	subit
	lda	score
	clc
	adc	pen
	sta	score
	lda	score+1
	adc	pen+1
	sta	score+1
	jmp	advance
subit:
	lda	score
	sec
	sbc	pen
	sta	score
	lda	score+1
	sbc	pen+1
	sta	score+1
advance:
	lda	base
	clc
	adc	#8
	sta	base
	cmp	#16
	bcs	done
	jmp	sideloop
done:
	lda	score
	ldx	score+1
	rts

.endproc

.else

	.code
.proc	_pawnStructScore: near
	lda	#0
	tax
	rts
.endproc

.endif
