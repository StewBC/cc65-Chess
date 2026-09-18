# agondev / ez80-none-elf-clang.  MOS binary for Agon Light, Light 2, Console8.
agon_FAMILY := agondev
agon_EMUCMD := recipes/run-agon.sh
agon_TEST_DEPS := $(BUILDDIR)/agon/chess.bin
agon_TEST_ARG  := $(abspath $(BUILDDIR)/agon/chess.bin)

AGON_BIN := $(BUILDDIR)/agon/chess.bin
