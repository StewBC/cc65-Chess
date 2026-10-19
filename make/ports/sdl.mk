# native desktop port; opt-in, like term.  no SDL flags reach other ports.
sdl_FAMILY := sdl2
sdl_EMUCMD := $(BUILDDIR)/sdl/chess
sdl_TEST_ARG :=
PKG_CONFIG ?= pkg-config
SDL_CFLAGS := -Os -funsigned-char -DPLAT_CURSOR_JUMP -I$(SRCDIR) \
	$(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
