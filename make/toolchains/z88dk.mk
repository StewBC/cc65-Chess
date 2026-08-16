# zcc.  one driver invocation — z88dk does not want a per-file object graph.
# intermediates (zcc_opt.def, the map) stay in build/spectrum/.

ifneq ($(TARGETLIST),spectrum)
$(BUILDDIR)/spectrum/chess $(BUILDDIR)/spectrum/chess.tap $(BUILDDIR)/spectrum/chess.sna:
	$(MAKE) TARGETS=spectrum all
endif

ifeq ($(TARGETLIST),spectrum)
ifeq ($(spectrum_AVAILABLE),1)

SPECTRUM_SRCS := $(ENGINE_C) $(SRCDIR)/spectrum/platSpectrum.c
SPECTRUM_ABS  := $(abspath $(SPECTRUM_SRCS))

$(BUILDDIR)/spectrum:
	$(call MKDIR,$@)

# zcc writes zcc_opt.def into cwd.  work in the port dir.
$(SPECTRUM_BIN): $(SPECTRUM_SRCS) | $(BUILDDIR)/spectrum
	cd $(BUILDDIR)/spectrum && $(ZCC) $(SPECTRUM_CFLAGS) \
		-I$(abspath $(SRCDIR)) -o chess $(SPECTRUM_ABS)
	@python3 make/spectrum-map.py $(BUILDDIR)/spectrum/chess.map

# do not use zcc -create-app: at this ORG it emits a REM-as-program tap
# that LOAD "" will not run.  write CLEAR / LOAD CODE / USR ourselves.
$(SPECTRUM_TAP): $(SPECTRUM_BIN) make/spectrum-maketap.py
	python3 make/spectrum-maketap.py $(SPECTRUM_BIN) -o $@ \
		--org $(SPECTRUM_ORG) --name chess

$(SPECTRUM_SNA): $(SPECTRUM_BIN)
	$(Z88DK_APPMAKE) +zx --sna -b $(SPECTRUM_BIN) -o $@ \
		--org $(SPECTRUM_ORG) --sna-sp $(SPECTRUM_SP)

all: $(SPECTRUM_BIN) $(SPECTRUM_TAP) $(SPECTRUM_SNA)

PROGRAM := $(SPECTRUM_BIN)

clean:
	$(call RMFILES,$(SPECTRUM_BIN) $(SPECTRUM_TAP) $(SPECTRUM_SNA))
	$(call RMFILES,$(BUILDDIR)/spectrum/chess.map $(BUILDDIR)/spectrum/zcc_opt.def)

endif # available
endif # spectrum
