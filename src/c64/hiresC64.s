
;	hiresC64.s
;	cc65 Chess
;
;	Created by Stefan Wessels, May 2020.
;
;-----------------------------------------------------------------------

.include "zeropage.inc"

;-----------------------------------------------------------------------
.export _plat_gfxFill, _plat_showStrXY, _plat_showPiece, _plat_colorStringXY, _plat_colorFill
.import popa

;-----------------------------------------------------------------------
.define VIC_BASE_RAM			$C000
.define SCREEN_RAM				VIC_BASE_RAM + $2000
.define CHARMAP_RAM             VIC_BASE_RAM + $2800

;-----------------------------------------------------------------------
.code

;-----------------------------------------------------------------------
; Utility - Turn tmp2 (y) and tmp3 (x) into a pointer in ptr2 that points
; in VIC_BASE_RAM with x, y being col and row with col and row being
; 8 pixels
.proc plat_ptr2AtHgrXY

    lda tmp2                                    ; load y
    asl                                         ; ptr2 points at start with 320 = 256 + 64 so
    asl                                         ; y in hi + 64 * y in lo/hi + SCREEN_RAM hi = address
    asl 
    asl 
    rol ptr2 + 1
    asl 
    rol ptr2 + 1
    asl 
    rol ptr2 + 1
    sta ptr2
    lda tmp2                                    ; add y in hi
    adc ptr2 + 1
    adc #>(VIC_BASE_RAM)                        ; and vic ram in hi (vic ram aligned, lo = 0)
    sta ptr2 + 1                                ; ptr2 now points at the correct row

    stx tmp2                                    ; re-use tmp2, clear for possible carry
    lda tmp3                                    ; x * 8 is offset into row where this col starts
    asl 
    asl 
    asl 
    rol tmp2
    adc ptr2
    sta ptr2 
    lda tmp2 
    adc ptr2 + 1
    sta ptr2 + 1                                ; ptr2 now points at the start address on-screen, incl. x
    
    rts

.endproc 

;-----------------------------------------------------------------------
; Utility - Turn tmp2 (y) and ptr1 (x) into a pointer in ptr1 that points
; at SCREEN_RAM[x+y*40]
.proc plat_ptr2AtScrXY

    lda tmp2                                    ; load y
    asl                                         ; * 32
    asl 
    asl 
    asl 
    rol ptr1 + 1 
    asl 
    rol ptr1 + 1 
    adc ptr1                                    ; add x in
    sta ptr1
    bcc :+
    inc ptr1 + 1
:
    lda tmp2                                    ; start with y
    asl                                         ; 8 * + 32 * = 40 *
    asl 
    asl 
    adc ptr1 
    sta ptr1
    lda ptr1 + 1
    adc #>(SCREEN_RAM)                          ; add screen ram hi as lo is $0 (aligned)
    sta ptr1 + 1

    rts

.endproc 

;-----------------------------------------------------------------------
; void plat_gfxFill(char fill, char x, char y, char w, char h)
; where y & h are multiples of 8 and x and w are in columns (i.e.
; bytes not pixels).  Fill the 8x8 aligned rectangle with fill.
.proc _plat_gfxFill

    sta tmp1                                    ; h

    ldx #0
    stx ptr1 + 1                                ; prep ptr1 hi for possible carry
    stx ptr2 + 1                                ; prep ptr2 hi for possible carry

    jsr popa                                    ; w
    asl                                         ; w * 8 - w in cols and 8 bytes per col till next col
    asl 
    asl 
    rol ptr1 + 1                                ; ptr1 now contains number of bytes to fill per row
    sta ptr1

    jsr popa
    sta tmp2                                    ; y in tmp2
    jsr popa
    sta tmp3                                    ; x in tmp3
    jsr plat_ptr2AtHgrXY

    sec                                         ; calculate (320 - width) as the "stride"
    lda #$40
    sbc ptr1
    sta tmp2
    lda #1
    sbc ptr1 + 1
    sta tmp3

    jsr popa                                    ; fill
    sta tmp4

    sei                                         ; stop interrupts
    lda 1                                       ; kernel out
    and #$FD
    sta 1

loop:
    lda tmp4
    ldy ptr1 + 1                                ; check hi byte of width to fill
    beq low                                     ; width totals less than 256 bytes, no hi copy
    ldy #0                                      ; fill 256 bytes
:
    sta (ptr2), y
    dey 
    bne :-
    inc ptr2 + 1                                ; adjust the write pointer by 256 bytes

low:
    ldy ptr1                                    ; get the low bytes top copy
    beq rowdone                                 ; if none left to copy then row is done
:
    dey 
    sta (ptr2), y                               ; write the block of bytes < 256
    bne :-

rowdone:
    dec tmp1                                    ; one more row done
    beq done                                    ; when wrap all rows are done
    clc 
    lda ptr1
    adc ptr2
    bcc :+
    inc ptr2 + 1
    clc
:
    adc tmp2                                    ; add stride to get to next row
    sta ptr2 
    lda tmp3 
    adc ptr2 + 1
    sta ptr2 + 1
    bne loop                                    ; bra

done:
    lda 1                                       ; kernel in
    ora #$02
    sta 1
    cli                                         ; resume interrupts

    rts

.endproc

;-----------------------------------------------------------------------
; void plat_colorFill(char color, char x, char y, char w, char h)
; Fills a rectangle in SCREEN_RAM with color.
.proc _plat_colorFill

    sta tmp1                                    ; h

    lda #0
    sta ptr1 + 1                                ; prep ptr2 hi for possible carry

    jsr popa                                    ; w
    sta tmp3

    jsr popa
    sta tmp2                                    ; y in tmp2
    jsr popa
    sta ptr1                                    ; x in tmp3
    jsr plat_ptr2AtScrXY

    jsr popa                                    ; color
    sta tmp2

loop:
    lda tmp2
    ldy tmp3                                    ; get the low bytes top copy
    beq rowdone                                 ; if none left to copy then row is done
:
    dey 
    sta (ptr1), y                               ; write the color
    bne :-

rowdone:
    dec tmp1                                    ; one more row done
    beq done                                    ; when wrap all rows are done
    lda ptr1 
    adc #$28
    sta ptr1 
    bcc loop 
    inc ptr1 + 1
    clc
    bne loop                                    ; bra

done:
    rts
.endproc 

;-----------------------------------------------------------------------
; void plat_showStrXY(char x, char Y, char *str)
; Display the string pointed at by str in hires using the character set
; which must be located at CHARACTER_RAM.  This must be Set 2 which
; includes lower case characters
.proc _plat_showStrXY

    sta ptr1
    stx ptr1 + 1

    ldx #0
    stx ptr2 + 1                                ; init hi value for possibly shifting carry into

    jsr popa
    sta tmp2                                    ; y in tmp2
    jsr popa
    sta tmp3                                    ; x in tmp3
    jsr plat_ptr2AtHgrXY

    sei                                         ; stop interrupts
    lda 1                                       ; kernel out
    and #$FD
    sta 1

    ldy #0
    sty tmp2
loop:
    ldy tmp2
    lda (ptr1), y                               ; character
    beq done
    cmp #'z'
    bpl :+
    and #63
:
    and #127
    ldy #7
    ldx #0
    stx read + 2
    asl                                         ; *8 for bytes
    rol read + 2
    asl 
    rol read + 2
    asl 
    rol read + 2
    sta read + 1
    lda #>(CHARMAP_RAM)
    adc read + 2
    sta read + 2
read:
    lda $ffff, y
    sta (ptr2), y
    dey
    bpl read 
    lda ptr2 
    adc #8
    sta ptr2 
    bcc :+
    inc ptr2 + 1
    clc
:
    inc tmp2
    bne loop

done:
    lda 1                                       ; kernel in
    ora #$02
    sta 1
    cli                                         ; resume interrupts
    rts

.endproc

;-----------------------------------------------------------------------
; void plat_colorStringXY(char color, char x, char y, char *str)
; Uses str only for length.  Writes color into SCREEN_RAM at
; SCREEN_RAM[i+x+y*40] where i iterates the string [while(str[i]) i++].
.proc _plat_colorStringXY

    sta ptr2                                    ; store the str pointer in ptr2
    stx ptr2 + 1

    lda #0                                      ; clear ptr1 hi for carry
    sta ptr1 + 1

    jsr popa                                    ; y
    sta tmp2                                    ; save y in tmp 2
    jsr popa                                    ; x
    sta ptr1                                    ; save x in ptr1 lo
    jsr plat_ptr2AtScrXY

    jsr popa                                    ; get the color
    tax                                         ; save it in x
    ldy #0                                      ; start at 0
:
    lda (ptr2), y
    beq done                                    ; as long as string has a valid character
    txa                                         ; put color in a 
    sta (ptr1), y                               ; and write to SCREEN_RAM
    iny 
    bne :-

done:
    rts

.endproc

;-----------------------------------------------------------------------
; void plat_showPiece(char x, char Y, const char *src)
; copies 3 * 32 consecutive bytes from src to a rectangle in VIC RAM at x, y
; The rect is 4 bytes by 24 bytes (rows) so 96 bytes total
.proc _plat_showPiece

    sta ptr1                                    ; store the pointer to the piece
    stx ptr1 + 1

    ldx #0                                      ; plat_ptr2AtHgrXY assumes x = 0
    stx ptr2 + 1                                ; init hi value for possibly shifting carry into

    jsr popa
    sta tmp2                                    ; y in tmp2
    jsr popa
    sta tmp3                                    ; x in tmp3
    jsr plat_ptr2AtHgrXY                        ; calculate offset into ptr2

    sei                                         ; stop interrupts
    lda 1                                       ; kernel out
    and #$FD
    sta 1

    lda #3                                      ; need to do 3 loops for a whole piece
    sta tmp1

loop:
    ldy #31                                     ; 32 bytes to copy
:
    lda (ptr1), y                               ; load src
    eor (ptr2), y                               ; merge with dest
    sta (ptr2), y                               ; save at dest
    dey
    bpl :-                                      ; do all 32

    dec tmp1                                    ; another 3rd done
    beq done                                    ; all done?

    lda ptr1                                    ; more to do - move src data ptr along by 32
    adc #32
    sta ptr1
    bcc :+
    clc 
    inc ptr1 + 1
:
    lda ptr2                                    ; move graphics pointer along by 320 (row down)
    adc #<320
    sta ptr2
    lda ptr2 + 1
    adc #>320
    sta ptr2 + 1
    bne loop                                    ; bra

done:
    lda 1                                       ; kernel in
    ora #$02
    sta 1
    cli                                         ; resume interrupts

    rts

.endproc

