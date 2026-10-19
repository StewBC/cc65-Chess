# Platform recipes

How to *run* the ports this machine can actually run. Building is `make` from
the repo root — `make help` is the verb list, `make list` says what this
machine can compile. Match output, profiles and other generated artifacts
still go under `scratch/`, which is gitignored.

Several ports need a compiler installation path or environment settings:
for example `RETRO68` for mac68k, `PATH`/`ZCCCFG` for spectrum and
`LLVM_MOS_SDK` for mega65. The notes below give those settings. Use `make list`
to check availability; a default build skips unavailable compilers.

*Running* uses a second set, one per emulator, prefixed to the binary so a
`make <port> test` can reach an emulator that is not on `PATH`: `VICE_HOME`
(c64, c64.chr, plus4), `CX16_HOME`, `AWIN_HOME`, `ORIC_HOME`, `ATARI_HOME`,
`RP6502_HOME`. Each is a prefix, so it needs a **trailing slash**:
`VICE_HOME=/Applications/vice-arm64-gtk3-3.10/bin/`.  Agon is the
exception: `FAE_HOME` is the Fab Agon Emulator *install root*, not a
prefix, because the emulator has to be launched from that directory. MEGA65
uses full paths: `XEMU_MEGA65` for the executable and `MEGA65_ROM` for the ROM.
Amiga does the same: `AMIGA_FS_UAE` for the emulator and `AMIGA_KICKSTART`
for the Kickstart 1.3 ROM. The compiler is the Bartman extension's
`m68k-amiga-elf-gcc`, or `AMIGA_GCC` and `AMIGA_ELF2HUNK` when it lives
somewhere else.

The SDL2 desktop port runs directly, with no emulator or ROM.

| File | Target |
|---|---|
| [sdl-desktop.md](sdl-desktop.md) | Native SDL2 desktop (`make sdl test`) |
| [apple2-debugging.md](apple2-debugging.md) | Apple II under `../a2m` |
| [plus4-debugging.md](plus4-debugging.md) | Plus/4 under VICE `xplus4` |
| [altirra-bridge-usage.md](altirra-bridge-usage.md) | Atari under AltirraSDL / AltirraBridge |
| [using-c64m.md](using-c64m.md) | C64 under `../c64m` (control port) |
| [rp6502-emulator.md](rp6502-emulator.md) | Picocomputer under `rp6502-emu` (headless, scripted) |
| [spectrum-z88dk.md](spectrum-z88dk.md) | ZX Spectrum (`make spectrum`) |
| [mac68k-retro68.md](mac68k-retro68.md) | Macintosh 68k (`make mac68k`), Basilisk II |
| [ti99-mame.md](ti99-mame.md) | TI-99/4A (`make ti99`), Homebrew MAME |
| [coco3-cmoc.md](coco3-cmoc.md) | Color Computer 3 (`make coco3`), XRoar |
| [mega65-llvm-mos.md](mega65-llvm-mos.md) | Native MEGA65 (`make mega65`), LLVM-MOS and Xemu |
| [agon-emulator.md](agon-emulator.md) | Agon Light (`make agon`), Fab Agon Emulator |
| [amiga-fs-uae.md](amiga-fs-uae.md) | Amiga 500 (`make amiga`), FS-UAE |
| [run-agon.sh](run-agon.sh) | Fab Agon — also what `make agon test` runs |
| [run-amiga.sh](run-amiga.sh) | FS-UAE A500 — also what `make amiga test` runs |
| [run-coco3.sh](run-coco3.sh) | XRoar coco3 — also what `make coco3 test` runs |
| [run-spectrum.sh](run-spectrum.sh) | ZEsarUX 48K — also what `make spectrum test` runs |
| [build-a2m-profile.sh](build-a2m-profile.sh) | Bootable Apple II profile image |
| [vice/](vice/) | Plus/4 binary-monitor helpers used by the Plus/4 note |

`doc/measuring.md` is the instrument list. This folder contains the platform how-tos
those instruments assume.
