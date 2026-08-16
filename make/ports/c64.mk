c64_FAMILY := cc65
c64_CC65   := c64
c64_EMUCMD := $(VICE_HOME)x64sc -kernal kernal -VICIIdsize -autostart

C64_BIN := $(BUILDDIR)/c64/$(NAME)
C64_PRG := $(BUILDDIR)/c64/$(NAME).prg
C64_D64 := $(BUILDDIR)/c64/$(NAME).d64

C1541 ?= c1541

.PHONY: prg d64

prg: $(C64_PRG)
d64: $(C64_D64)

$(C64_PRG): $(C64_BIN)
	$(call CP,$< $@)

$(C64_D64): $(C64_BIN)
	$(C1541) -format "$(NAME)","01" d64 $@ -attach $@ -write $< $(NAME).prg
