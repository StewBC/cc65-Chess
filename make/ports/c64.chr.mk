# character-mode C64.  cl65 -t is still c64; the sources live in src/c64.chr/.
c64.chr_FAMILY := cc65
c64.chr_CC65   := c64
c64.chr_EMUCMD := $(VICE_HOME)x64sc -kernal kernal -VICIIdsize -autostart

C64CHR_BIN := $(BUILDDIR)/c64.chr/$(NAME)
C64CHR_PRG := $(BUILDDIR)/c64.chr/$(NAME).prg

.PHONY: cprg

cprg: $(C64CHR_PRG)

$(C64CHR_PRG): $(C64CHR_BIN)
	$(call CP,$< $@)
