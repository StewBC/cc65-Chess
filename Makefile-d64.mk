D64 = $(PROGRAM).d64

C1541 ?= c1541.exe

# Unix or Windows
ifeq ($(shell echo),)
	CP = cp $1
else
	CP = copy $(subst /,\,$1)
endif

REMOVES += $(D64)

.PHONY: d64
d64: $(D64)

$(D64): $(PROGRAM).c64
	$(C1541) -format "$(PROGRAM)","01" d64 $(PROGRAM).d64 -attach $(PROGRAM).d64 -write $< $(PROGRAM).prg
