# z88dk / zcc, not cc65.  tap + sna are produced as part of `all`.
spectrum_FAMILY := z88dk
spectrum_EMUCMD := recipes/run-spectrum.sh
spectrum_TEST_DEPS := $(BUILDDIR)/spectrum/chess.sna
spectrum_TEST_ARG  := $(abspath $(BUILDDIR)/spectrum/chess.sna)

SPECTRUM_ORG    := 24000
SPECTRUM_SP     := 65367
SPECTRUM_BIN    := $(BUILDDIR)/spectrum/chess
SPECTRUM_TAP    := $(BUILDDIR)/spectrum/chess.tap
SPECTRUM_SNA    := $(BUILDDIR)/spectrum/chess.sna
SPECTRUM_CFLAGS := +zx -vn -O3 -clib=default -zorg=$(SPECTRUM_ORG) \
	-pragma-define:REGISTER_SP=$(SPECTRUM_SP) \
	-DSEARCH_ARENA=256 -m
