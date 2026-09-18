# agondev clang/ld.  one port.  product is a MOS binary for the SD card.

ifneq ($(TARGETLIST),agon)
$(BUILDDIR)/agon/chess.bin:
	$(MAKE) TARGETS=agon all
endif

ifeq ($(TARGETLIST),agon)
ifeq ($(agon_AVAILABLE),1)

AGONDEV_PREFIX  := $(shell $(AGONDEV_CONFIG) --prefix)
AGONDEV_CC      := $(AGONDEV_PREFIX)/bin/ez80-none-elf-clang
AGONDEV_LD      := $(AGONDEV_PREFIX)/bin/ez80-none-elf-ld
AGONDEV_SETNAME := $(AGONDEV_PREFIX)/bin/agondev-setname
AGONDEV_INCLUDE := $(AGONDEV_PREFIX)/include
AGONDEV_LIB     := $(AGONDEV_PREFIX)/lib
AGONDEV_LDSCRIPT:= $(AGONDEV_PREFIX)/config/linker.conf

AGON_ENGINE := $(ENGINE_C)
AGON_PLAT_C := $(SRCDIR)/agon/platAgon.c $(SRCDIR)/agon/dataAgon.c
AGON_C      := $(AGON_ENGINE) $(AGON_PLAT_C)
AGON_OBJS   := $(addprefix $(OBJDIR)/agon/,$(notdir $(AGON_C:.c=.o)))
AGON_LINK   := $(BUILDDIR)/agon/chess.noname.bin

# unsigned char is not optional — PIECE_WHITE is bit 7 of a char.
# RAM_START/SIZE match agondev's defaults: 448K from $40000, well above MOS.
AGON_CFLAGS := -mllvm -z80-gas-style -mllvm -z80-print-zero-offset \
	-nostdinc -isystem $(AGONDEV_INCLUDE) -target ez80-none-elf \
	-DAGONDEV -Oz -Wa,-march=ez80+full -fno-threadsafe-statics \
	-funsigned-char -std=gnu99 -I$(SRCDIR)
AGON_LDFLAGS := -defsym=RAM_START=0x40000 -defsym=RAM_SIZE=0x70000 \
	-defsym=_has_exit_handler=0 -Map=$(BUILDDIR)/agon/chess.map \
	-T $(AGONDEV_LDSCRIPT) --oformat binary

$(BUILDDIR)/agon $(OBJDIR)/agon:
	$(call MKDIR,$@)

$(OBJDIR)/agon/%.o: $(SRCDIR)/%.c | $(OBJDIR)/agon
	$(AGONDEV_CC) $(AGON_CFLAGS) -c -o $@ $<

$(OBJDIR)/agon/%.o: $(SRCDIR)/agon/%.c | $(OBJDIR)/agon
	$(AGONDEV_CC) $(AGON_CFLAGS) -c -o $@ $<

$(AGON_LINK): $(AGON_OBJS) | $(BUILDDIR)/agon
	$(AGONDEV_LD) $(AGON_LDFLAGS) -o $@ $(AGON_OBJS) -L$(AGONDEV_LIB) -l agon

$(AGON_BIN): $(AGON_LINK)
	cp $< $@
	$(AGONDEV_SETNAME) $@ chess.bin >/dev/null

all: $(AGON_BIN)

PROGRAM := $(AGON_BIN)

clean:
	$(call RMFILES,$(AGON_BIN) $(AGON_LINK) $(BUILDDIR)/agon/chess.map)
	$(call RMFILES,$(AGON_OBJS))

endif # available
endif # agon
