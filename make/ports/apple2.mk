apple2_FAMILY  := cc65
apple2_CC65    := apple2
apple2_LDFLAGS := --start-addr 0x4000 -Wl -D -Wl __HIMEM__=0xBF00
apple2_EMUCMD  := $(AWIN_HOME)AppleWin.exe -d1

APPLE2_BIN := $(BUILDDIR)/apple2/$(NAME)
APPLE2_PO  := $(BUILDDIR)/apple2/$(NAME).po
APPLE2_DSK := $(BUILDDIR)/apple2/$(NAME).dsk
APPLE2_VOL := $(subst -,.,$(NAME))

CA ?= cadius
AC ?= ac.jar

.PHONY: po dsk

po: $(APPLE2_PO)
dsk: $(APPLE2_DSK)

$(APPLE2_PO): $(APPLE2_BIN)
	$(call CP,apple2/template.po $@)
	$(call CP,$(subst \,/,$(shell $(CL65) --print-target-path)/apple2/util/loader.system) $(BUILDDIR)/apple2/chess.system#FF2000)
	$(call CP,$(APPLE2_BIN) $(BUILDDIR)/apple2/chess#064000)
	$(NO_CONV) $(CA) addfile $@ /$(APPLE2_VOL) $(BUILDDIR)/apple2/chess.system#FF2000
	$(NO_CONV) $(CA) addfile $@ /$(APPLE2_VOL) $(BUILDDIR)/apple2/chess#064000
	$(RM) $(BUILDDIR)/apple2/chess.system#FF2000
	$(RM) $(BUILDDIR)/apple2/chess#064000

$(APPLE2_DSK): $(APPLE2_BIN)
	$(call CP,apple2/template.dsk $@)
	java -jar $(AC) -p  $@ chess.system sys < $(subst \,/,$(shell $(CL65) --print-target-path)/apple2/util/loader.system)
	java -jar $(AC) -as $@ chess        bin < $(APPLE2_BIN)
