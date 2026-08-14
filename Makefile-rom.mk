# The Picocomputer's native package is a .rp6502 ROM - not a ROM in the old
# sense but a file holding a memory image the RIA loads into RAM before it
# lets the 6502 out of reset.  rp6502.py builds one from the raw cl65 output.
#
# The tool is from picocomputer/vscode-cc65 and lives in rp6502/ next to the
# Apple II's disk templates in apple2/.

ROM = $(PROGRAM)-rp6502.rp6502

RP6502 ?= python3 rp6502/rp6502.py

REMOVES += $(ROM)

.PHONY: rom
rom: $(ROM)

# -a and -r are both 0x200 because that is where rp6502.cfg loads and where
# crt0.s puts _init, and cl65 -t rp6502 writes a headerless binary so neither
# can be read out of the file.  'create' merges into an existing ROM rather
# than replacing it, so the old one has to go first
$(ROM): $(PROGRAM).rp6502
	-$(call RMFILES,$(ROM))
	$(RP6502) -a 0x200 -r 0x200 -o $@ create $<
