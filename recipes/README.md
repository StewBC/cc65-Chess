# Emulator recipes

How to *run* the targets this machine can actually run. Match output, profiles
and other generated artifacts still go under `scratch/`, which is gitignored.

| File | Target |
|---|---|
| [apple2-debugging.md](apple2-debugging.md) | Apple II under `../a2m-v2` |
| [plus4-debugging.md](plus4-debugging.md) | Plus/4 under VICE `xplus4` |
| [altirra-bridge-usage.md](altirra-bridge-usage.md) | Atari under AltirraSDL / AltirraBridge |
| [using-c64m.md](using-c64m.md) | C64 under `../c64m` (control port) |
| [rp6502-emulator.md](rp6502-emulator.md) | Picocomputer under `rp6502-emu` (headless, scripted) |
| [spectrum-z88dk.md](spectrum-z88dk.md) | ZX Spectrum under z88dk / zcc (`make spectrum`) |
| [run-spectrum.sh](run-spectrum.sh) | ZEsarUX 48K — also what `make spectrum test` runs |
| [build-a2m-profile.sh](build-a2m-profile.sh) | Bootable Apple II profile image |
| [vice/](vice/) | Plus/4 binary-monitor helpers used by the Plus/4 note |

`doc/measuring.md` is the instrument list. This folder is the emulator how-to
those instruments assume.
