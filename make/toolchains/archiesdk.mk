# ArchieSDK supplies startup, libc, SWIs and the ARM2 linker defaults.
ifeq ($(TARGETLIST),archie)
ifeq ($(archie_AVAILABLE),1)

ARCHIE_SRCS := $(ENGINE_C) $(SRCDIR)/archie/platArchie.c \
	$(SRCDIR)/c64/dataC64.c $(SRCDIR)/coco3/font.c
ARCHIE_OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/archie/%.o,$(ARCHIE_SRCS))
ARCHIE_DEPS := $(ARCHIE_OBJS:.o=.d)

$(OBJDIR)/archie/%.o: $(SRCDIR)/%.c make/ports/archie.mk make/toolchains/archiesdk.mk
	$(call MKDIR,$(@D))
	$(ARCHIE_CC) $(ARCHIE_CFLAGS) -MMD -MP -c -o $@ $<

$(ARCHIE_ELF): $(ARCHIE_OBJS)
	$(call MKDIR,$(@D))
	$(ARCHIE_CC) $(ARCHIE_CFLAGS) -Wl,-Map,$(BUILDDIR)/archie/chess.map \
		-o $@ $(ARCHIE_OBJS) -L$(ARCHIESDK)/tools/lib -lOSLib32

$(ARCHIE_BIN): $(ARCHIE_ELF)
	$(ARCHIESDK)/tools/bin/arm-archie-objcopy -O binary $< $@

$(ARCHIE_HOST_ZIP): $(ARCHIE_BIN) make/archie-package.py $(SRCDIR)/c64/dataC64.c LICENSE
	python3 make/archie-package.py $(BUILDDIR)/archie

$(ARCHIE_NATIVE_ZIP): $(ARCHIE_HOST_ZIP)
	@test -f $@ || python3 make/archie-package.py $(BUILDDIR)/archie

all: $(ARCHIE_HOST_ZIP) $(ARCHIE_NATIVE_ZIP)
PROGRAM := $(ARCHIE_BIN)

clean:
	$(call RMFILES,$(ARCHIE_BIN) $(ARCHIE_ELF) $(ARCHIE_HOST_ZIP) $(ARCHIE_NATIVE_ZIP) $(BUILDDIR)/archie/chess.map)
	$(call RMFILES,$(ARCHIE_OBJS) $(ARCHIE_DEPS))
	rm -rf $(BUILDDIR)/archie/!Chess

-include $(ARCHIE_DEPS)
endif
endif
