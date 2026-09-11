# CMOC.  Disk BASIC .bin in 32K; GIME framebuffer is off the 6809 map.
coco3_FAMILY := cmoc
coco3_EMUCMD := recipes/run-coco3.sh
coco3_TEST_DEPS := $(BUILDDIR)/coco3/chess.bin
coco3_TEST_ARG  := $(abspath $(BUILDDIR)/coco3/chess.bin)

COCO3_BIN := $(BUILDDIR)/coco3/chess.bin
COCO3_DSK := $(BUILDDIR)/coco3/chess.dsk
