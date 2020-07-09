
; hiresAtari.s
; cc65 Chess
;
; Created by Stefan Wessels, July 2020.
;
;-----------------------------------------------------------------------
.include "atari.inc"
.include "zeropage.inc"

;-----------------------------------------------------------------------
; Display-list related defenitions
scrn    = $9100                                 ; screen starts here
scnd    = $a000                                 ; lower part of screen here
top     = ((scnd - scrn) / $28)                 ; display list entries before scnd
bot     = ($c0-top)                             ; display list entries after scnd
mode    = $0f                                   ; mode to run the screen at

;-----------------------------------------------------------------------
; Display list - mode 0x0f (320x192, 2 color).
; Goes in its own segment so it won't cross a boundry
.segment "DLIST"

displayList:
        .byte $70,$70,$70                       ; 24 blank lines
        .byte $40 + mode,<scrn,>scrn            ; Mode $0x + LMS, setting screen memory to $9000
        .repeat top-1                           ; 102 lines of mode $0x (incl LMS row above)
            .byte mode
        .endrep
        .byte $40 +mode, <(scnd), >(scnd)       ; clear the 4k boundry and start another row of mode $0x
        .repeat bot-1                           ; another 90 lines of mode $0x (incl. LMS row above)
            .byte mode
        .endrep
        .byte $41,<displayList,>displayList     ; Vertical Blank jump to start of displayList

;-----------------------------------------------------------------------
; lookup to the start of a graphics row
rowL:
    .repeat top, I                              ; $66 rows at $9000
        .byte <(scrn + I *40)
    .endrep
    .repeat bot, I                              ; $5a rows at $a000
        .byte <(scnd + I *40)
    .endrep

rowH:
    .repeat top, I 
        .byte >(scrn + I *40)
    .endrep
    .repeat bot, I 
        .byte >(scnd + I *40)
    .endrep

;-----------------------------------------------------------------------
; The code to draw in hires
.code

;-----------------------------------------------------------------------
.import popa

;-----------------------------------------------------------------------
; the functions exported to "C"
.export _plat_atariInit
.export _plat_gfxFill
.export _plat_showPiece
.export _plat_showStrXY

;-----------------------------------------------------------------------
; Init the hires screen with teh display list
.proc _plat_atariInit

    lda #0                                      ; stop the dma
    sta DMACTL
    lda #<displayList                           ; install the new display list
    sta SDLSTL
    lda #>displayList
    sta SDLSTH
    lda #$22                                    ; resume the DMA
    sta DMACTL

    rts

.endproc

;-----------------------------------------------------------------------
; void plat_gfxFill(char fill, char x, char y, char w, char h)
.proc _plat_gfxFill

    sta ptr2 + 1                                ; h was in acc
    jsr popa                                    ; w
    sta ptr2
    jsr popa                                    ; y
    sta tmp1
    jsr popa                                    ; x
    sta tmp2
    jsr popa                                    ; fill
    sta tmp3

rowStart:
    ldy tmp1                                    ; get the row top
    lda rowL, y                                 ; get the address where the row starts
    sta ptr1
    lda rowH, y
    sta ptr1 + 1
    lda tmp3                                    ; get the fill byte
    ldx ptr2                                    ; width in x
    ldy tmp2                                    ; and col x in y register
:
    sta (ptr1), y                               ; write the fill byte
    iny                                         ; memory is linear
    dex                                         ; one less column to do
    bne :-                                      ; do for all columns
    inc tmp1                                    ; go down one row
    dec ptr2 + 1                                ; one less row to do in height
    bne rowStart                                ; repeat till all height rows done
    rts 

.endproc

;-----------------------------------------------------------------------
; void plat_showPiece(char x, char Y, const char *src)
.proc _plat_showPiece

    sta read + 1                                ; store the pointer to the piece
    stx read + 2
    jsr popa                                    ; y
    sta tmp1
    jsr popa                                    ; x
    sta tmp2 

    lda #22                                     ; pieces are 22 heigh
    sta tmp4 

    ldx #0                                      ; start at the 1st byte of the piece
loop:
    ldy tmp1                                    ; get the current piece row
    lda rowL, y                                 ; look up teh row start address in memory
    sta ptr1 
    lda rowH, y 
    sta ptr1 + 1
    lda #3                                      ; a piece is 3 bytes wide
    sta tmp3                                    ; col counter
    ldy tmp2                                    ; the col x goes into y register

read:
    lda $ffff, x                                ; read a byte from the piece
    eor (ptr1), y                               ; eor with teh screen
    sta (ptr1), y                               ; and save back to the screen
    inx                                         ; next byte in piece
    iny                                         ; next byte on screen
    dec tmp3                                    ; one less col byte to do
    bne read                                    ; if not all done, do the rest of the cols on this row
    inc tmp1                                    ; next row
    dec tmp4                                    ; one less row to do of the 22
    bne loop                                    ; if not all done, do the next row

    rts

.endproc

;-----------------------------------------------------------------------
; void plat_showStrXY(char x, char Y, char *str)
.proc _plat_showStrXY

    sta strRead + 1                             ; pointer to the strin comes in as a, x regsiters
    stx strRead + 2

    jsr popa                                    ; get the y
    asl                                         ; mult it by 8
    asl 
    asl 
    sta tmp1                                    ; y * 8
    jsr popa 
    sta tmp2                                    ; x

loop:
    lda #0
    sta fontRead + 2                            ; prep the hi byte

strRead:
    lda $ffff                                   ; get the character
    bne :+                                      ; if not 0, not at end of string
    rts                                         ; end of string, all done
:
    cmp #96                                     ; if lowercase
    bcs :+                                      ; then all is well
    sec                                         ; uppercase and numbers need to shift
    sbc #32                                     ; down by 32 to be aligned with the ROM font
:
    inc strRead + 1                             ; next char in string
    bne :+
    inc strRead + 2
:
    asl                                         ; ROM char is 8 wide so mult string char * 8
    rol fontRead + 2
    asl 
    rol fontRead + 2
    asl 
    rol fontRead + 2
    sta fontRead + 1
    clc 
    lda CHBAS                                   ; add the character rom hi
    adc fontRead + 2                            ; to the character pointer
    sta fontRead + 2
    
    ldx #0                                      ; start at first row of 8 rows in a char
    ldy tmp1                                    ; y reg is the piwel row to draw at

charPlot:
    sty tmp3                                    ; copy the pixel row
    lda rowL, y                                 ; look up the screen address of the row
    sta ptr1 
    lda rowH, y 
    sta ptr1 + 1
    ldy tmp2                                    ; put the col into the y reg

fontRead:
    lda $ffff, x                                ; read the font defenition for this char row (char row in x)
    sta (ptr1), y                               ; store to the screen
    ldy tmp3                                    ; get the screen pixel row in y
    iny                                         ; go down a row
    inx                                         ; one more of the 8 character rows done
    cpx #8                                      ; all 8 done
    bne charPlot                                ; if not, keep going
    inc tmp2                                    ; next column on screeen
    jmp loop                                    ; do the next character in the string

.endproc
