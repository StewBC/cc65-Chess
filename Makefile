# cc65 Chess — one dispatcher, many ports, more than one compiler.
#
#   make              every default port whose compiler is on PATH
#   make list         available vs skipped
#   make c64          one port
#   make apple2 po    binary + disk image
#   make spectrum     z88dk
#   make spectrum test
#   make term         host curses → build/term/chessterm
#   make check        native suite in tests/
#
# products land in build/<port>/.  objects in build/obj/<port>/.
# a new compiler is a toolchain file plus a thin port file — see make/.
#
# the cc65 compile rules are descended from oliver schmidt's generic makefile
# (v1.3.0, with patryk łogiewa).  the abstraction is now "port", not "cl65 -t".

include make/common.mk
include $(sort $(wildcard make/ports/*.mk))

# --- availability --------------------------------------------------------

apple2_AVAILABLE   := $(HAVE_CL65)
atari_AVAILABLE    := $(HAVE_CL65)
atmos_AVAILABLE    := $(HAVE_CL65)
c64_AVAILABLE      := $(HAVE_CL65)
c64.chr_AVAILABLE  := $(HAVE_CL65)
plus4_AVAILABLE    := $(HAVE_CL65)
cx16_AVAILABLE     := $(HAVE_CL65)
rp6502_AVAILABLE   := $(HAVE_RP6502)
spectrum_AVAILABLE := $(HAVE_ZCC)
term_AVAILABLE     := $(HAVE_CC)

apple2_SKIP   := cl65 not on PATH
atari_SKIP    := cl65 not on PATH
atmos_SKIP    := cl65 not on PATH
c64_SKIP      := cl65 not on PATH
c64.chr_SKIP  := cl65 not on PATH
plus4_SKIP    := cl65 not on PATH
cx16_SKIP     := cl65 not on PATH
rp6502_SKIP   := cl65 has no rp6502 target (needs the picocomputer fork)
spectrum_SKIP := zcc not on PATH
term_SKIP     := $(CC) not on PATH

OPTIONAL_DEFAULTS := $(if $(HAVE_ZCC),spectrum)
BUILD_PORTS := $(strip $(foreach p,$(DEFAULT_PORTS) $(OPTIONAL_DEFAULTS),$(if $($(p)_AVAILABLE),$(p))))

# `make spectrum` (and `make spectrum test`) selects that port.  TARGETS= on
# the command line still wins, so the recursive $(MAKE) TARGETS=$t keeps working.
ifeq ($(origin TARGETS),command line)
  TARGETLIST := $(subst $(COMMA),$(SPACE),$(TARGETS))
else
  FIRSTGOAL := $(firstword $(MAKECMDGOALS))
  ifneq ($(filter $(FIRSTGOAL),$(PORT_NAMES)),)
    TARGETLIST := $(FIRSTGOAL)
  else
    TARGETLIST := $(BUILD_PORTS)
  endif
endif

include make/toolchains/cc65.mk
include make/toolchains/z88dk.mk
include make/toolchains/host.mk

# port files declare packaging first (po, atr, ...).  without this, a bare
# `make TARGETS=c64` builds the first of those instead of the c64 binary.
.DEFAULT_GOAL := all

.PHONY: all test clean zap tidy love help list check $(PORT_NAMES)

$(PORT_NAMES): all

# --- single port vs many -------------------------------------------------

ifeq ($(words $(TARGETLIST)),1)

ifeq ($($(TARGETLIST)_AVAILABLE),1)

test: all $($(TARGETLIST)_TEST_DEPS)
	$(PREEMUCMD)
	$($(TARGETLIST)_EMUCMD) $(if $(filter undefined,$(origin $(TARGETLIST)_TEST_ARG)),$(PROGRAM),$($(TARGETLIST)_TEST_ARG))
	$(POSTEMUCMD)

else

all test:
	@echo "port $(TARGETLIST): $($(TARGETLIST)_SKIP)" >&2
	@false

clean:
	@true

endif

else # many ports, or none

all:
ifeq ($(strip $(TARGETLIST)),)
	@echo "no compilers found.  install cl65 and/or zcc, or run make list" >&2
	@false
else
	$(foreach t,$(TARGETLIST),$(MAKE) TARGETS=$t all$(NEWLINE))
endif

test:
	@echo "name a port:  make c64 test   or   make TARGETS=c64 test" >&2
	@false

clean:
	$(foreach t,$(TARGETLIST),$(MAKE) TARGETS=$t clean$(NEWLINE))

endif

# --- always --------------------------------------------------------------

help:
	@echo "cc65 Chess"
	@echo
	@echo "  make                 default ports whose compiler is here"
	@echo "  make list            available vs skipped, and why"
	@echo "  make help            this text"
	@echo "  make <port>          one port  (apple2 atari atmos c64 c64.chr"
	@echo "                                  plus4 cx16 rp6502 spectrum term)"
	@echo "  make apple2 po       Apple II + ProDOS image"
	@echo "  make atari atr       Atari + ATR"
	@echo "  make c64 d64         C64 + D64  (also: prg cprg cxprg tap rom dsk)"
	@echo "  make spectrum        ZX Spectrum tap + sna (z88dk)"
	@echo "  make spectrum test   build and run ZEsarUX"
	@echo "  make term            host curses binary"
	@echo "  make check           native suite (tests/)"
	@echo "  make clean           this selection's products"
	@echo "  make tidy            leftover binaries in the repo root"
	@echo "  make zap             build/ and leftover root binaries"
	@echo
	@echo "products:  build/<port>/"
	@echo "objects:   build/obj/<port>/"
	@echo "cc65 options persist in build/.options  (default: optsize)"
	@echo "new compiler: make/toolchains/<family>.mk + make/ports/<name>.mk"

list:
	@printf '%-10s %-8s %s\n' "port" "family" "status"
	@$(foreach p,$(PORT_NAMES),printf '%-10s %-8s %s\n' \
		'$(p)' '$($(p)_FAMILY)' \
		'$(if $($(p)_AVAILABLE),available,skipped — $($(p)_SKIP))';)

check:
	$(MAKE) -C tests test

tidy:
	-$(call RMFILES,$(ROOT_LEFTOVERS))
	-rm -rf obj atari.atr 2>/dev/null || true

zap:
	-$(call RMFILES,$(addprefix $(BUILDDIR)/,$(foreach p,$(PORT_NAMES),$(p)/*)))
	-$(call RMFILES,$(addprefix $(OBJDIR)/,$(foreach p,$(PORT_NAMES),$(p)/*)))
	-$(call RMFILES,$(STATEFILE))
	-$(call RMFILES,$(ROOT_LEFTOVERS))
	-$(call RMFILES,$(wildcard $(OBJDIR)/*/*))
	-rm -rf $(BUILDDIR) obj atari.atr 2>/dev/null || true

love:
	@echo "Not war, eh?"
