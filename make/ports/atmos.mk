atmos_FAMILY := cc65
atmos_CC65   := atmos
atmos_EMUCMD := $(ORIC_HOME)Oricutron.exe -t

ATMOS_BIN := $(BUILDDIR)/atmos/$(NAME)
ATMOS_TAP := $(BUILDDIR)/atmos/$(NAME).tap

.PHONY: tap

tap: $(ATMOS_TAP)

$(ATMOS_TAP): $(ATMOS_BIN)
	$(call CP,$< $@)
