cx16_FAMILY := cc65
cx16_CC65   := cx16
cx16_EMUCMD := $(CX16_HOME)x16emu -run -prg

CX16_BIN := $(BUILDDIR)/cx16/$(NAME)
CX16_PRG := $(BUILDDIR)/cx16/$(NAME).prg

.PHONY: cxprg

cxprg: $(CX16_PRG)

$(CX16_PRG): $(CX16_BIN)
	$(call CP,$< $@)
