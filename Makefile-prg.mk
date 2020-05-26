PRG = cc65-Chess-c64.prg

# Unix or Windows
ifeq ($(shell echo),)
	CP = cp $1
else
	CP = copy $(subst /,\,$1)
endif

REMOVES += $(PRG)

.PHONY: prg
prg: $(PRG)

$(PRG): cc65-chess.c64
	$(call CP, $< $@)
