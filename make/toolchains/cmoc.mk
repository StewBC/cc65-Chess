# CMOC / lwtools.  one port.  product is a Disk BASIC .bin and an RS-DOS .dsk.

ifneq ($(TARGETLIST),coco3)
$(BUILDDIR)/coco3/chess.bin $(BUILDDIR)/coco3/chess.dsk:
	$(MAKE) TARGETS=coco3 all
endif

ifeq ($(TARGETLIST),coco3)
ifeq ($(coco3_AVAILABLE),1)

COCO3_CC := $(CMOC)

COCO3_ENGINE := $(ENGINE_C)
COCO3_PLAT_C := $(SRCDIR)/coco3/platCoco3.c $(SRCDIR)/coco3/font.c $(SRCDIR)/coco3/dataCoco3.c
COCO3_C      := $(COCO3_ENGINE) $(COCO3_PLAT_C)
COCO3_OBJS   := $(addprefix $(OBJDIR)/coco3/,$(notdir $(COCO3_C:.c=.o)))
COCO3_DEPS   := $(addprefix $(OBJDIR)/coco3/,$(notdir $(COCO3_C:.c=.d)))

# Disk BASIC LOADM lands in the 32K that is RAM at boot ($0000-$7FFF).
# $0E00 leaves the DP and BASIC work area; $7E00 is the ceiling before
# the stack at $7F00.  GIME framebuffer is in unmapped blocks $30-$33.
COCO3_CMOCFLAGS := --coco -funsigned-char -O2 \
	--org=0E00 --limit=7E00 --initial-s=7F00 \
	-I$(SRCDIR)/coco3 -I$(SRCDIR) -DSEARCH_ARENA=256

$(BUILDDIR)/coco3 $(OBJDIR)/coco3:
	$(call MKDIR,$@)

$(OBJDIR)/coco3/%.o: $(SRCDIR)/%.c | $(OBJDIR)/coco3
	$(COCO3_CC) $(COCO3_CMOCFLAGS) --deps -c -o $@ $<

$(OBJDIR)/coco3/%.o: $(SRCDIR)/coco3/%.c | $(OBJDIR)/coco3
	$(COCO3_CC) $(COCO3_CMOCFLAGS) --deps -c -o $@ $<

$(COCO3_BIN): $(COCO3_OBJS) | $(BUILDDIR)/coco3
	$(COCO3_CC) $(COCO3_CMOCFLAGS) -o $@ $(COCO3_OBJS)
	@python3 make/coco3-size.py $@

$(COCO3_DSK): $(COCO3_BIN)
	$(RM) $@
	imgtool create coco_jvc_rsdos $@
	imgtool put coco_jvc_rsdos $@ $(COCO3_BIN) CHESS.BIN --ftype=binary --ascii=binary

all: $(COCO3_BIN) $(COCO3_DSK)

PROGRAM := $(COCO3_BIN)

clean:
	$(call RMFILES,$(COCO3_BIN) $(COCO3_DSK))
	$(call RMFILES,$(COCO3_OBJS) $(COCO3_DEPS))
	$(call RMFILES,$(BUILDDIR)/coco3/chess.map)

-include $(COCO3_DEPS)

endif # available
endif # coco3
