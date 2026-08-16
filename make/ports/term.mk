# host curses build.  not part of a bare `make` — development, not a port image.
term_FAMILY := host
term_EMUCMD := $(BUILDDIR)/term/chessterm
term_TEST_ARG :=

TERM_BIN := $(BUILDDIR)/term/chessterm
TERM_CFLAGS := -I$(SRCDIR) -funsigned-char
TERM_LIBS   := -lcurses
