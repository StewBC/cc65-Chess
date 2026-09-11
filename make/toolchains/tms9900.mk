# tms9900-gcc.  one port.  products are an EA5 image and a paged378 RPK.

ifneq ($(TARGETLIST),ti99)
$(BUILDDIR)/ti99/chess.elf $(BUILDDIR)/ti99/chess.rpk $(BUILDDIR)/ti99/chess.ea5:
	$(MAKE) TARGETS=ti99 all
endif

ifeq ($(TARGETLIST),ti99)
ifeq ($(ti99_AVAILABLE),1)

TI99_CC      := $(TMS9900_GCC)
TI99_AS      := $(dir $(TMS9900_GCC))tms9900-as
TI99_LD      := $(dir $(TMS9900_GCC))tms9900-ld
TI99_OBJCOPY := $(dir $(TMS9900_GCC))tms9900-objcopy
TI99_LIBGCC  := $(shell $(TMS9900_GCC) -print-libgcc-file-name)

TI99_ENGINE := $(ENGINE_C)
TI99_PLAT_C := $(SRCDIR)/ti99/platTi99.c $(SRCDIR)/ti99/string.c $(SRCDIR)/ti99/font.c
TI99_C      := $(TI99_ENGINE) $(TI99_PLAT_C)
TI99_OBJS   := $(addprefix $(OBJDIR)/ti99/,$(notdir $(TI99_C:.c=.o))) \
	$(OBJDIR)/ti99/crt0.o $(OBJDIR)/ti99/hw.o
TI99_DEPS   := $(addprefix $(OBJDIR)/ti99/,$(notdir $(TI99_C:.c=.d)))

$(BUILDDIR)/ti99 $(OBJDIR)/ti99:
	$(call MKDIR,$@)

$(OBJDIR)/ti99/%.o: $(SRCDIR)/%.c | $(OBJDIR)/ti99
	$(TI99_CC) $(TI99_CFLAGS) -c -o $@ $<

$(OBJDIR)/ti99/%.o: $(SRCDIR)/ti99/%.c | $(OBJDIR)/ti99
	$(TI99_CC) $(TI99_CFLAGS) -c -o $@ $<

$(OBJDIR)/ti99/crt0.o: $(SRCDIR)/ti99/crt0.asm | $(OBJDIR)/ti99
	$(TI99_AS) $< -o $@

$(OBJDIR)/ti99/hw.o: $(SRCDIR)/ti99/hw.asm | $(OBJDIR)/ti99
	$(TI99_AS) $< -o $@

$(OBJDIR)/ti99/header.o: $(SRCDIR)/ti99/header.asm | $(OBJDIR)/ti99
	$(TI99_AS) $< -o $@

$(BUILDDIR)/ti99/header.elf: $(OBJDIR)/ti99/header.o | $(BUILDDIR)/ti99
	$(TI99_LD) $< --section-start .text=6000 -o $@

$(BUILDDIR)/ti99/header.bin: $(BUILDDIR)/ti99/header.elf
	$(TI99_OBJCOPY) -O binary $< $@
	@$(dir $(TMS9900_GCC))tms9900-nm $< | awk '/ _start$$/ { printf "cart _start = 0x%s\n", $$1 }'

$(TI99_BIN): $(TI99_OBJS) $(SRCDIR)/ti99/link.ld | $(BUILDDIR)/ti99
	$(TI99_LD) -T $(SRCDIR)/ti99/link.ld -Map $(BUILDDIR)/ti99/chess.map \
		-o $@ $(OBJDIR)/ti99/crt0.o $(filter-out $(OBJDIR)/ti99/crt0.o,$(TI99_OBJS)) \
		$(TI99_LIBGCC)
	@$(dir $(TMS9900_GCC))tms9900-nm $@ | python3 -c '\
import sys;\
d={};\
[d.update({l.split()[-1]:int(l.split()[0],16)}) for l in sys.stdin if len(l.split())>=3];\
print("ti99: .text %d  .data_end 0x%x  .bss_end 0x%x"%(d.get("_etext",0)-0xa000,d.get("_data_end",0),d.get("_bss_end",0)))'

$(BUILDDIR)/ti99/text.bin: $(TI99_BIN)
	$(TI99_OBJCOPY) -O binary -j .text $< $@

$(BUILDDIR)/ti99/data.bin: $(TI99_BIN)
	$(TI99_OBJCOPY) -O binary -j .data $< $@

$(TI99_EA5): $(BUILDDIR)/ti99/text.bin $(BUILDDIR)/ti99/data.bin make/ti99-ea5.py
	python3 make/ti99-ea5.py $(BUILDDIR)/ti99/text.bin $(BUILDDIR)/ti99/data.bin $@

$(TI99_RPK): $(BUILDDIR)/ti99/header.bin $(BUILDDIR)/ti99/text.bin \
		$(BUILDDIR)/ti99/data.bin make/ti99-mkcart.py
	python3 make/ti99-mkcart.py $(BUILDDIR)/ti99/header.bin \
		$(BUILDDIR)/ti99/text.bin $(BUILDDIR)/ti99/data.bin $(BUILDDIR)/ti99

all: $(TI99_EA5) $(TI99_RPK)

PROGRAM := $(TI99_RPK)

clean:
	$(call RMFILES,$(TI99_BIN) $(TI99_EA5) $(TI99_RPK))
	$(call RMFILES,$(BUILDDIR)/ti99/header.elf $(BUILDDIR)/ti99/header.bin)
	$(call RMFILES,$(BUILDDIR)/ti99/text.bin $(BUILDDIR)/ti99/data.bin)
	$(call RMFILES,$(BUILDDIR)/ti99/chessC.bin $(BUILDDIR)/ti99/layout.xml)
	$(call RMFILES,$(BUILDDIR)/ti99/chess.map $(BUILDDIR)/ti99/boot.lua)
	$(call RMFILES,$(TI99_OBJS) $(TI99_DEPS) $(OBJDIR)/ti99/header.o)

-include $(TI99_DEPS)

endif # available
endif # ti99
