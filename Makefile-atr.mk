# If ATARIDOS is commented out, the disk will be MyPicodos 4.05
# if ATARIDOS is not commented out, set ATARIDOSTYPE to the 
# desired type (say Dos25 for Dos 2.5) and put the 
# sys files for the correct version in the ataridos folder
# (i.e. dos.sys and dup.sys). Only tested with Dos25 and MyPicodos

ATR = cc65-Chess.atr
# ATARIDOS = ataridos
ATARIDOSTYPE = Dos25
ATARIDSK = atari.atr
DIR2ATR ?= dir2atr.exe

# Unix or Windows
ifeq ($(shell echo),)
	CP = cp $1
else
	CP = copy $(subst /,\,$1)
endif

# Just the files, not the atari.atr folder.  Don't know how to extend the zap: in the Makefile
REMOVES += $(ATR) $(ATARIDSK)/cc65-Chess $(ATRDOSOBJS)

.PHONY: ATR
atr: $(ATR)

$(ATARIDSK):
	$(call MKDIR,$@)

$(ATARIDSK)/%.sys: 
	$(call CP,$(ATARIDOS)/$(notdir $(@)) $@)

# Different based on ATARIDOS or MyPicoDos
ifeq ($(ATARIDOS),)
ATARIDOSTYPE = MyPicoDOS405
ATRDOSOBJS :=
else
# I don't know of a better way to "copy if needed" the .sys files from one folder to another
ATRDOSSRCS += $(wildcard $(ATARIDOS)/*.sys)
ATRDOSOBJS := $(addsuffix .sys,$(basename $(addprefix $(ATARIDSK)/,$(notdir $(ATRDOSSRCS)))))
endif

$(ATR): cc65-Chess.atari $(ATARIDSK) $(ATRDOSOBJS)
	$(call CP,$< $(ATARIDSK)/cc65-Chess)
	$(DIR2ATR) -b $(ATARIDOSTYPE) cc65-Chess.atr $(ATARIDSK)
