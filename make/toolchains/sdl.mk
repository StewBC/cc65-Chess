# SDL2 links the same engine, original C64 pieces and the portable 8x8 font.
ifeq ($(TARGETLIST),sdl)
ifeq ($(sdl_AVAILABLE),1)
SDL_BIN := $(BUILDDIR)/sdl/chess
SDL_SRCS := $(ENGINE_C) $(SRCDIR)/sdl/platSDL.c \
	$(SRCDIR)/c64/dataC64.c $(SRCDIR)/coco3/font.c
SDL_HEADERS := $(wildcard $(SRCDIR)/*.h) $(SRCDIR)/c64/dataC64.h

$(BUILDDIR)/sdl:
	$(call MKDIR,$@)

$(SDL_BIN): $(SDL_SRCS) $(SDL_HEADERS) make/ports/sdl.mk make/toolchains/sdl.mk | $(BUILDDIR)/sdl
	$(CC) $(SDL_CFLAGS) -o $@ $(SDL_SRCS) $(SDL_LIBS)

all: $(SDL_BIN)
PROGRAM := $(SDL_BIN)

SDL_CHECK := $(BUILDDIR)/sdl/check
$(SDL_CHECK): tests/sdl.c $(SDL_SRCS) $(SDL_HEADERS) make/ports/sdl.mk make/toolchains/sdl.mk | $(BUILDDIR)/sdl
	$(CC) $(SDL_CFLAGS) -Wall -Wextra -Wno-char-subscripts -o $@ tests/sdl.c \
		$(filter-out $(SRCDIR)/main.c $(SRCDIR)/sdl/platSDL.c,$(SDL_SRCS)) $(SDL_LIBS)

.PHONY: sdl-check
sdl-check: $(SDL_CHECK)
	$(call MKDIR,scratch)
	SDL_VIDEODRIVER=dummy $(SDL_CHECK)

clean:
	$(call RMFILES,$(SDL_BIN) $(SDL_CHECK))
endif
endif
