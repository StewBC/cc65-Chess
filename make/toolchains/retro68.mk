# m68k-apple-macos-gcc + Rez.  one port.  products are a MacBinary, a
# disk image, and an APPL

ifneq ($(TARGETLIST),mac68k)
$(BUILDDIR)/mac68k/$(NAME).bin:
	$(MAKE) TARGETS=mac68k all
endif

ifeq ($(TARGETLIST),mac68k)
ifeq ($(mac68k_AVAILABLE),1)

MAC68K_SRCS := $(ENGINE_C) $(SRCDIR)/mac68k/platMac68k.c $(SRCDIR)/mac68k/dataMac68k.c
MAC68K_OBJS := $(addprefix $(OBJDIR)/mac68k/,$(notdir $(MAC68K_SRCS:.c=.o)))
MAC68K_DEPS := $(MAC68K_OBJS:.o=.d)

$(BUILDDIR)/mac68k $(OBJDIR)/mac68k:
	$(call MKDIR,$@)

$(OBJDIR)/mac68k/%.o: $(SRCDIR)/%.c | $(OBJDIR)/mac68k
	$(RETRO68_CC) $(MAC68K_CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/mac68k/%.o: $(SRCDIR)/mac68k/%.c | $(OBJDIR)/mac68k
	$(RETRO68_CC) $(MAC68K_CFLAGS) -MMD -MP -c -o $@ $<

$(MAC68K_CODE): $(MAC68K_OBJS) | $(BUILDDIR)/mac68k
	$(RETRO68_CC) $(MAC68K_CFLAGS) $(MAC68K_LDFLAGS) -o $@ $(MAC68K_OBJS)

# Rez writes type/creator but not kHasBundle.  patch the MacBinary, then
# recopy it onto the disk image so the HFS catalog carries the flag —
# otherwise Finder never reads the BNDL and shows the generic app icon.
$(MAC68K_BIN) $(MAC68K_APPL) $(MAC68K_DSK): $(MAC68K_CODE) $(MAC68K_R) make/mac68k-setbundle.py
	$(RETRO68_REZ) $(RETRO68_REZINC)/Retro68APPL.r $(MAC68K_R) \
		-I$(RETRO68_REZINC) \
		--copy $(MAC68K_CODE) \
		-o $(MAC68K_BIN) \
		-t APPL -c $(MAC68K_CREATOR) \
		--cc $(MAC68K_DSK) --cc $(MAC68K_APPL)
	python3 make/mac68k-setbundle.py $(MAC68K_BIN)
	$(RETRO68)/bin/hformat -f -l cc65-Chess $(MAC68K_DSK)
	$(RETRO68)/bin/hmount $(MAC68K_DSK)
	$(RETRO68)/bin/hcopy -m $(MAC68K_BIN) :
	$(RETRO68)/bin/humount

all: $(MAC68K_BIN)

PROGRAM := $(MAC68K_BIN)

clean:
	$(call RMFILES,$(MAC68K_CODE) $(MAC68K_BIN) $(MAC68K_APPL) $(MAC68K_DSK))
	$(call RMFILES,$(MAC68K_OBJS) $(MAC68K_DEPS))

-include $(MAC68K_DEPS)

endif # available
endif # mac68k
