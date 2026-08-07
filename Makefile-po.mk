NAME = chess
PO = $(NAME).po

CA ?= cadius

# Unix or Windows
ifeq ($(shell echo),)
	CP = cp $1
	MV = mv
	RM = rm
else
	CP = copy $(subst /,\,$1)
	MV = ren
	RM = del
endif

REMOVES += $(PO)

.PHONY: po
po: $(PO)

$(NAME).system:
	$(call CP, $(subst \,/,$(shell cl65 --print-target-path)/apple2/util/loader.system) $(NAME).system#FF2000)

$(PO): $(PROGRAM).apple2 $(NAME).system
	$(call CP, apple2/template.po $@)
	$(MV) $(PROGRAM).apple2 $(NAME)#060803
	$(CA) addfile $(NAME).po /$(subst -,.,$(PROGRAM)) $(NAME).system#FF2000
	$(CA) addfile $(NAME).po /$(subst -,.,$(PROGRAM)) $(NAME)#060803
	$(RM) $(NAME).system#FF2000
	$(RM) $(NAME)#060803