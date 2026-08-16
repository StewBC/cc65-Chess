# shared facts.  every toolchain and port includes this via the top Makefile.

NAME     := cc65-Chess
SRCDIR   := src
BUILDDIR := build
OBJDIR   := $(BUILDDIR)/obj

# a port is a name, not a cc65 target.  toolchains and availability live elsewhere.
PORT_NAMES := apple2 atari atmos c64 c64.chr plus4 cx16 rp6502 spectrum term

# built by a bare `make` when the compiler is present.  rp6502 stays off this
# list even if the picocomputer fork is installed — it was never a default.
# spectrum joins in when zcc is on PATH.  term is host-only: `make term`.
DEFAULT_PORTS := apple2 atari atmos c64 c64.chr plus4 cx16

ENGINE_C := $(sort $(wildcard $(SRCDIR)/*.c))
ENGINE_S := $(sort $(wildcard $(SRCDIR)/*.s))

# emulator homes — prefix the binary if it is not on PATH.
VICE_HOME   :=
CX16_HOME   :=
AWIN_HOME   :=
ORIC_HOME   :=
ATARI_HOME  :=
RP6502_HOME :=

PREEMUCMD  :=
POSTEMUCMD :=

STATEFILE := $(BUILDDIR)/.options

COMMA := ,
SPACE := $(N/A) $(N/A)
define NEWLINE


endef
# do not remove the two empty lines above

# native win32 make + cmd.exe vs everyone else.  same test as the oliver
# schmidt makefile this replaced: `echo` with no args is empty on sh.
ifeq ($(shell echo),)
  MKDIR   = mkdir -p $1
  RMDIR   = rmdir $1
  RMFILES = $(RM) $1
  CP      = cp $1
  MV      = mv
  WHICH   = command -v $1 2>/dev/null
  NO_CONV =
else
  MKDIR   = mkdir $(subst /,\,$1)
  RMDIR   = rmdir $(subst /,\,$1)
  RMFILES = $(if $1,del /f $(subst /,\,$1))
  CP      = copy $(subst /,\,$1)
  MV      = ren
  WHICH   = where $1
  NO_CONV =
endif

# MSYS/Git Bash rewrites /cc65.Chess as a path.  cadius needs the slash.
ifneq ($(findstring sh,$(SHELL)),)
  NO_CONV = MSYS_NO_PATHCONV=1
endif

CL65          := $(shell $(call WHICH,cl65))
ZCC           := $(shell $(call WHICH,zcc))
Z88DK_APPMAKE := $(shell $(call WHICH,z88dk-appmake))

HAVE_CL65 := $(if $(CL65),1)
HAVE_ZCC  := $(if $(ZCC),1)
HAVE_CC   := $(if $(shell $(call WHICH,$(CC))),1)

ifeq ($(HAVE_CL65),1)
  CL65_TARGET_PATH := $(shell $(CL65) --print-target-path 2>/dev/null)
  HAVE_RP6502 := $(if $(wildcard $(CL65_TARGET_PATH)/rp6502),1)
endif

# leftover root names from before products lived in build/.  zap removes them.
ROOT_LEFTOVERS := \
	cc65-Chess.apple2 cc65-Chess.atari cc65-Chess.atmos \
	cc65-Chess.c64 cc65-Chess.c64.chr cc65-Chess.cx16 cc65-Chess.plus4 \
	cc65-Chess.rp6502 \
	cc65-Chess.po cc65-Chess.dsk cc65-Chess.atr cc65-Chess.tap \
	cc65-Chess.d64 cc65-Chess-c64.prg cc65-Chess-chr.prg cc65-Chess-cx16.prg \
	cc65-Chess-rp6502.rp6502 \
	chess.po chess.system chess.system\#FF2000 chess\#064000 \
	Makefile.options
