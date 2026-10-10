# ARM2 / ARM250, RISC OS 3.11, A3010 with 2 MB.  C64 presentation.
archie_FAMILY := archiesdk
archie_EMUCMD := recipes/run-archie.sh
archie_TEST_ARG :=

ARCHIE_BIN := $(BUILDDIR)/archie/chess,ff8
ARCHIE_ELF := $(BUILDDIR)/archie/chess.elf
ARCHIE_HOST_ZIP := $(BUILDDIR)/archie/Chess-HostFS.zip
ARCHIE_NATIVE_ZIP := $(BUILDDIR)/archie/Chess-RISCOS.zip

# size optimisation, unsigned piece bytes, and the existing mouse cursor hook.
ARCHIE_CFLAGS := -mcpu=arm2 -msoft-float -mno-thumb-interwork -Os \
	-funsigned-char -std=gnu89 -Wall -Wextra -Wno-char-subscripts -I$(SRCDIR) \
	-DPLAT_CURSOR_JUMP -isystem $(ARCHIESDK)/SDK/include
