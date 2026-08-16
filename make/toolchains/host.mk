# host cc.  term is the only port on this toolchain; tests/ stays its own makefile.

ifneq ($(TARGETLIST),term)
$(BUILDDIR)/term/chessterm:
	$(MAKE) TARGETS=term all
endif

ifeq ($(TARGETLIST),term)
ifeq ($(term_AVAILABLE),1)

TERM_SRCS := $(ENGINE_C) $(SRCDIR)/term/platTerm.c

$(BUILDDIR)/term:
	$(call MKDIR,$@)

$(TERM_BIN): $(TERM_SRCS) | $(BUILDDIR)/term
	$(CC) $(TERM_CFLAGS) -o $@ $(TERM_SRCS) $(TERM_LIBS)

all: $(TERM_BIN)

PROGRAM := $(TERM_BIN)
term_EMUCMD := $(TERM_BIN)

clean:
	$(call RMFILES,$(TERM_BIN))

endif # available
endif # term
