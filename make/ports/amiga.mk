# A500.  Bartman's m68k-amiga-elf-gcc.  joins a bare `make` when it is found.
amiga_FAMILY := bartman
amiga_EMUCMD := recipes/run-amiga.sh
amiga_TEST_ARG :=

AMIGA_ELF := $(BUILDDIR)/amiga/a.elf
AMIGA_EXE := $(BUILDDIR)/amiga/boot/a.exe

# 68000, not 68020.  unsigned char is not optional — the engine treats
# PIECE_WHITE as bit 7 of a char.  PLAT_CURSOR_JUMP is the mouse, and it
# stays in this file so the machines without a pointer do not compile it.
# -Wno-unknown-pragmas: undo.c still carries cc65's bss-name pragma.
# -ffreestanding: this port has no libc.  gcc -Os rewrites a function
# named strlen into "jra strlen" (a __builtin_strlen tail call).
# -fno-builtin-strlen does not stop that.  -ffreestanding does, and the
# splash's first centred() comes back.
AMIGA_CFLAGS := -m68000 -Os -funsigned-char -std=gnu99 -Wall \
	-Wno-unknown-pragmas -Wno-char-subscripts -Wno-volatile-register-var \
	-ffreestanding \
	-ffunction-sections -fdata-sections \
	-I$(SRCDIR) -DPLAT_CURSOR_JUMP
