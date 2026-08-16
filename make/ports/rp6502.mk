# same driver as the other 6502 ports, different compiler — the picocomputer
# fork of cc65.  stock cl65 has no rp6502 target; make list will say so.
rp6502_FAMILY := cc65
rp6502_CC65   := rp6502
rp6502_EMUCMD := $(RP6502_HOME)rp6502-emu $(BUILDDIR)/rp6502/$(NAME).rp6502 --tmpdrive --

RP6502_BIN := $(BUILDDIR)/rp6502/$(NAME)
RP6502_ROM := $(BUILDDIR)/rp6502/$(NAME).rp6502

RP6502 ?= python3 rp6502/rp6502.py

.PHONY: rom

rom: $(RP6502_ROM)

# -a and -r are both 0x200 because that is where rp6502.cfg loads and where
# crt0.s puts _init.  cl65 -t rp6502 writes a headerless binary.  `create`
# merges into an existing ROM, so the old one has to go first.
$(RP6502_ROM): $(RP6502_BIN)
	-$(call RMFILES,$(RP6502_ROM))
	$(RP6502) -a 0x200 -r 0x200 -o $@ create $<

# `make test` should boot the ROM, not the raw binary.
rp6502_TEST_DEPS := $(RP6502_ROM)
rp6502_TEST_ARG  :=
