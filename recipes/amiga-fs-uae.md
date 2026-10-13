# Amiga 500 (Bartman gcc and FS-UAE)

68000, Kickstart 1.3, a 320×256 low-resolution screen. The board is the C64
layout: 32×24 pieces from `src/c64/dataC64.c`, the move log on the right, and
the four B/A/D numbers in colour. Escape is Backup. The mouse moves the cursor,
the left button is Return, and the right button is Backup.

The compiler is the one inside the Bartman Abyss extension, `m68k-amiga-elf-gcc`
15.1, target `m68k-amiga-elf`. It is not on `PATH`. `make` finds
`~/.vscode/extensions/bartmanabyss.amiga-debug-*/bin/darwin/opt/bin/m68k-amiga-elf-gcc`.
Set `AMIGA_GCC` and `AMIGA_ELF2HUNK` to point somewhere else. The extension's
own FS-UAE binary is x86_64 and does not run on this machine. The port is
booted with the arm64 FS-UAE that already reached the Kickstart insert-disk
screen.

```sh
make amiga
make amiga test
```

`make amiga` writes `build/amiga/boot/`. Kickstart runs `s/startup-sequence`,
which runs `a.exe`. There is no Workbench disk. Chip memory for the bitplanes
is allocated at runtime; the executable's own stack is 32K because the CLI
stack DOS provides is a few kilobytes.

`make amiga test` uses:

```sh
export AMIGA_FS_UAE=/Volumes/EXTERNAL/Applications/FS-UAE.app/Contents/MacOS/fs-uae
export AMIGA_KICKSTART="$HOME/Documents/FS-UAE/Kickstarts/amiga-os-130.rom"
```

The ROM and `rom.key` stay in the FS-UAE Kickstarts directory. They are not
copied into this tree. Quit from the menu closes the screen and returns to the
CLI that launched the program.
