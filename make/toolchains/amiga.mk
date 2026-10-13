# Bartman m68k-amiga-elf-gcc, then elf2hunk.  the product is a boot
# directory: Kickstart runs s/startup-sequence, which runs a.exe.

ifeq ($(TARGETLIST),amiga)
ifeq ($(amiga_AVAILABLE),1)

AMIGA_SRCS := $(ENGINE_C) $(SRCDIR)/amiga/platAmiga.c $(SRCDIR)/amiga/startup.c \
	$(SRCDIR)/amiga/mem.c $(SRCDIR)/c64/dataC64.c $(SRCDIR)/coco3/font.c
# start.o is first on purpose.  DOS runs the first byte of the first hunk,
# and that byte has to be _start.  gcc puts main in .text.startup.* which
# the default script gathers with .text.startup, in link order.
AMIGA_OBJS := $(OBJDIR)/amiga/amiga/start.o \
	$(OBJDIR)/amiga/amiga/c2p.o \
	$(patsubst $(SRCDIR)/%.c,$(OBJDIR)/amiga/%.o,$(AMIGA_SRCS))
AMIGA_DEPS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/amiga/%.d,$(AMIGA_SRCS))

$(OBJDIR)/amiga/%.o: $(SRCDIR)/%.c make/ports/amiga.mk make/toolchains/amiga.mk
	$(call MKDIR,$(@D))
	$(AMIGA_GCC) $(AMIGA_CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR)/amiga/%.o: $(SRCDIR)/%.s make/ports/amiga.mk make/toolchains/amiga.mk
	$(call MKDIR,$(@D))
	$(AMIGA_GCC) $(AMIGA_CFLAGS) -c -o $@ $<

$(AMIGA_ELF): $(AMIGA_OBJS)
	$(call MKDIR,$(@D))
	$(AMIGA_GCC) $(AMIGA_CFLAGS) -nostdlib \
		-Wl,--emit-relocs,--gc-sections,-Ttext=0,-e,_start,-Map,$(BUILDDIR)/amiga/a.map \
		-o $@ $(AMIGA_OBJS)

$(AMIGA_EXE): $(AMIGA_ELF) $(SRCDIR)/amiga/startup-sequence
	$(call MKDIR,$(dir $@)s)
	$(AMIGA_ELF2HUNK) $(AMIGA_ELF) $@
	chmod +x $@
	cp $(SRCDIR)/amiga/startup-sequence $(dir $@)s/startup-sequence

all: $(AMIGA_EXE)
PROGRAM := $(AMIGA_EXE)

clean:
	$(call RMFILES,$(AMIGA_ELF) $(AMIGA_EXE) $(BUILDDIR)/amiga/a.map)
	$(call RMFILES,$(AMIGA_OBJS) $(AMIGA_DEPS))
	rm -rf $(BUILDDIR)/amiga/boot

-include $(AMIGA_DEPS)

endif
endif
