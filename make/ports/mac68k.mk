# Retro68 / m68k-apple-macos-gcc.  joins a bare `make` when Retro68 is found.
mac68k_FAMILY := retro68

MAC68K_CREATOR := Cchs
MAC68K_CODE    := $(BUILDDIR)/mac68k/$(NAME).code.bin
MAC68K_BIN     := $(BUILDDIR)/mac68k/$(NAME).bin
MAC68K_APPL    := $(BUILDDIR)/mac68k/$(NAME).APPL
MAC68K_DSK     := $(BUILDDIR)/mac68k/$(NAME).dsk
MAC68K_R       := $(SRCDIR)/mac68k/chess.r $(SRCDIR)/mac68k/icon.r

# 68000, not 68030.  unsigned char is not optional — the engine treats
# PIECE_WHITE as bit 7 of a char.  PLAT_CURSOR_JUMP turns on the one piece
# of shared code only a pointing device needs — the machines without a
# mouse do not compile it, which is why it is here and not in human.c.
MAC68K_CFLAGS  := -funsigned-char -O2 -ffunction-sections -I$(SRCDIR) \
	-DPLAT_CURSOR_JUMP
MAC68K_LDFLAGS := -Wl,-gc-sections -Wl,--mac-strip-macsbug
