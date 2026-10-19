# Native SDL2 desktop version

Build and run from the repository root:

```sh
make sdl
make sdl test
# or run the executable directly:
./build/sdl/chess
```

This is an opt-in native host port. It uses the installed SDL2 development package
through `pkg-config`; override `PKG_CONFIG` if needed. The compiler uses `-Os` and
unsigned char, and the engine uses its unchanged four skill budgets. No emulator
or C64 ROM is involved. SDL2 2.0.18 or newer is needed for the resize test.

The default window is 960×720 and the minimum is 640×480. A 320×240 canvas contains
the original 320×200 C64 game layout, with a title and keyboard hints in the margins.
Nearest-neighbour scaling preserves the bitmap style; resizing keeps the aspect ratio
and letterboxes the unused area. High-DPI displays are supported.

The pieces come directly from `src/c64/dataC64.c`. The public-domain 8×8 font is
shared with the CoCo 3 and Amiga ports. The sixteen colours follow the Amiga port's
C64 palette. All assets are linked into the executable.

Cursor keys, RETURN, Escape (RUN/STOP), M, B, A, D, U and R retain their meanings.
Hover moves the board cursor, left click selects a piece and its destination,
and right click backs out. Menus support hover and left click as well as the keyboard.
Close the window to exit, or use the game's Quit menu.

The platform owns its framebuffer, so expose/resize events only present existing
pixels. Board redraws preserve the log globals around their undo-ring walk.
`PLAT_CURSOR_JUMP` is defined only in the SDL build flags; no shared engine code or
frozen platform interfaces were changed.

## Verification

```sh
make sdl sdl-check
make -C tests test
```

The SDL check uses the dummy video driver and software renderer. It sends SDL events
through the real menus and human player code: mouse and keyboard moves, undo/redo,
B/A/D, backing out of menus, resize coordinates, and ignored mouse input during CPU
polls. It also plays CPU turns at each skill level. Rendered board, overlay and menu
captures are written to `scratch/sdl-*.bmp`. Run `build/sdl/check` without the dummy
driver to exercise the native window and renderer with the same checks.

Verified on macOS with SDL2 2.32.74. Other SDL2 desktop systems have not been run here.
