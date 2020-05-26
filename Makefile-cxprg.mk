CXPRG = cc65-Chess-cx16.prg

# Unix or Windows
ifeq ($(shell echo),)
	CP = cp $1
else
	CP = copy $(subst /,\,$1)
endif

REMOVES += $(CXPRG)

.PHONY: cxprg
cxprg: $(CXPRG)

$(CXPRG): cc65-chess.cx16
	$(call CP, $< $@)
