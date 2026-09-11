* VDP and keyboard.  called from C: first arg R1, second R2, return R1.
* unsigned char lives in the high byte.

	pseg
	even
	def	vdp_wa
	def	vdp_reg
	def	vdp_put
	def	vdp_status
	def	kscan_col
	def	kscan_init
	def	ti_reboot

* void vdp_wa(unsigned int addr, unsigned int write)
* write != 0 sets the VDP write bit
vdp_wa:
	swpb	r1
	movb	r1, @>8C02
	swpb	r1
	mov	r2, r2
	jeq	vdp_wa_rd
	ori	r1, >4000
vdp_wa_rd:
	movb	r1, @>8C02
	b	*r11

* void vdp_reg(unsigned int reg, unsigned int val)
* val is an unsigned char in the high byte of R2
vdp_reg:
	movb	r2, @>8C02
	swpb	r1
	ori	r1, >8000
	movb	r1, @>8C02
	b	*r11

* void vdp_put(unsigned char b)
vdp_put:
	movb	r1, @>8C00
	b	*r11

* unsigned char vdp_status(void) — status in the high byte, GCC char return
vdp_status:
	clr	r1
	movb	@>8802, r1
	b	*r11

* raise the alpha-lock scan line so it does not sit on row 4
kscan_init:
	li	r12, >002A
	sbo	0
	b	*r11

* unsigned int kscan_col(unsigned int col)
* col 0-5.  returns a byte in the low 8 bits, 1 = pressed:
*  0x80 =  0x40 = space   0x20 = enter  0x10 = 9
*  0x08 = fctn  0x04 = shift   0x02 = ctrl   0x01 = X
kscan_col:
	sla	r1, 8
	li	r12, >0024
	ldcr	r1, 3
	li	r12, >0006
	clr	r1
	li	r2, 8
kscan_lp:
	sla	r1, 1
	tb	0
	jeq	kscan_up
	inc	r1
kscan_up:
	inct	r12
	dec	r2
	jne	kscan_lp
	b	*r11

* console reset.  the loader left the 378 on a payload bank, so GPL
* would read garbage at >6000 and hang (js99er does; hardware can too).
* put bank 0 back first — that is the header.
ti_reboot:
	limi	0
	clr	r12
	sbz	2
	clr	@>6000
	clr	@>83C4
	lwpi	>83E0
	blwp	@>0000
	jmp	ti_reboot
