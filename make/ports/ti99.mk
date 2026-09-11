# tms9900-gcc.  EA5 in 32K RAM; a paged378 cart copies it in so MAME can boot.
ti99_FAMILY := tms9900
ti99_EMUCMD := recipes/run-ti99.sh
ti99_TEST_DEPS := $(BUILDDIR)/ti99/chess.rpk
ti99_TEST_ARG  := $(abspath $(BUILDDIR)/ti99/chess.rpk)

TI99_BIN     := $(BUILDDIR)/ti99/chess.elf
TI99_EA5     := $(BUILDDIR)/ti99/chess.ea5
TI99_RPK     := $(BUILDDIR)/ti99/chess.rpk
TI99_CFLAGS  := -funsigned-char -fno-builtin -fno-function-cse -Os -std=c99 \
	-I$(SRCDIR) -I$(SRCDIR)/ti99 -DSEARCH_ARENA=256
