CPRG = cc65-Chess-chr.prg

# Unix or Windows
ifeq ($(shell echo),)
	CP = cp $1
else
	CP = copy $(subst /,\,$1)
endif

REMOVES += $(CPRG)

.PHONY: cprg
cprg: $(CPRG)

$(CPRG): cc65-chess.c64.chr
	$(call CP, $< $@)
