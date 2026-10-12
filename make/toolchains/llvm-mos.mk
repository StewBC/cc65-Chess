# native 45GS02 PRG; size optimisation, like every other shipping port.
ifeq ($(TARGETLIST),mega65)
ifeq ($(mega65_AVAILABLE),1)
MEGA65_C := $(ENGINE_C) $(wildcard $(SRCDIR)/mega65/*.c)
MEGA65_OBJS := $(addprefix $(OBJDIR)/mega65/,$(notdir $(MEGA65_C:.c=.o)))
MEGA65_FLAGS := -Oz -funsigned-char -std=gnu99 -I$(SRCDIR) -MMD -MP

$(OBJDIR)/mega65 $(BUILDDIR)/mega65:
	$(call MKDIR,$@)

$(OBJDIR)/mega65/%.o: $(SRCDIR)/%.c | $(OBJDIR)/mega65
	"$(LLVM_MOS_CC)" $(MEGA65_FLAGS) -c $< -o $@

$(OBJDIR)/mega65/%.o: $(SRCDIR)/mega65/%.c | $(OBJDIR)/mega65
	"$(LLVM_MOS_CC)" $(MEGA65_FLAGS) -c $< -o $@

$(MEGA65_BIN): $(MEGA65_OBJS) $(SRCDIR)/mega65/link.ld | $(BUILDDIR)/mega65
	cd $(SRCDIR)/mega65 && "$(LLVM_MOS_CC)" -Oz -Wl,-Map,$(abspath $(@:.prg=.map)) $(abspath $(MEGA65_OBJS)) -o $(abspath $@)

.PHONY: mega65-bench
mega65-bench: $(BUILDDIR)/mega65/bench.prg
$(OBJDIR)/mega65/bench.o: tests/mega65bench.c | $(OBJDIR)/mega65
	"$(LLVM_MOS_CC)" $(MEGA65_FLAGS) -c $< -o $@
$(BUILDDIR)/mega65/bench.prg: $(addprefix $(OBJDIR)/mega65/,engine.o eval.o search.o bench.o) $(SRCDIR)/mega65/link.ld | $(BUILDDIR)/mega65
	cd $(SRCDIR)/mega65 && "$(LLVM_MOS_CC)" -Oz -Wl,-Map,$(abspath $(@:.prg=.map)) $(abspath $(filter %.o,$^)) -o $(abspath $@)

-include $(MEGA65_OBJS:.o=.d) $(OBJDIR)/mega65/bench.d
all: $(MEGA65_BIN)
PROGRAM := $(MEGA65_BIN)
clean:
	$(call RMFILES,$(MEGA65_OBJS) $(MEGA65_OBJS:.o=.d) $(MEGA65_BIN) $(MEGA65_BIN:.prg=.map) $(MEGA65_BIN).elf $(BUILDDIR)/mega65/bench.prg $(BUILDDIR)/mega65/bench.prg.elf $(BUILDDIR)/mega65/bench.map $(OBJDIR)/mega65/bench.o $(OBJDIR)/mega65/bench.d)
endif
endif
