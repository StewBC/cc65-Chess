atari_FAMILY := cc65
atari_CC65   := atari
atari_EMUCMD := $(ATARI_HOME)Altirra64 /defprofile:800 /disk $(BUILDDIR)/atari/$(NAME).atr

ATARI_BIN := $(BUILDDIR)/atari/$(NAME)
ATARI_ATR := $(BUILDDIR)/atari/$(NAME).atr
ATARI_DSK := $(BUILDDIR)/atari/disk

# if ATARIDOS is set, copy its .sys files into the staging folder and pass
# that dos type to dir2atr.  otherwise MyPicoDOS 4.05, no extra files.
# ATARIDOS := ataridos
ATARIDOSTYPE ?= Dos25
DIR2ATR      ?= dir2atr

ifeq ($(ATARIDOS),)
  ATARIDOSTYPE = MyPicoDOS405
  ATRDOSOBJS :=
else
  ATRDOSSRCS := $(wildcard $(ATARIDOS)/*.sys)
  ATRDOSOBJS := $(addprefix $(ATARI_DSK)/,$(notdir $(ATRDOSSRCS)))
endif

.PHONY: atr

atr: $(ATARI_ATR)

$(ATARI_DSK):
	$(call MKDIR,$@)

$(ATARI_DSK)/%.sys: $(ATARIDOS)/%.sys | $(ATARI_DSK)
	$(call CP,$< $@)

$(ATARI_ATR): $(ATARI_BIN) $(ATARI_DSK) $(ATRDOSOBJS)
	$(call CP,$< $(ATARI_DSK)/$(NAME))
	$(DIR2ATR) -b $(ATARIDOSTYPE) $@ $(ATARI_DSK)
