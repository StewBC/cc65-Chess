# cl65 compile / link.  one port at a time — the top Makefile recurses.

# packaging rules need the binary even when this make process is building a
# different port (or several).  recurse rather than duplicate the link line.
define cc65_bin_stub
ifneq ($(TARGETLIST),$(1))
$(BUILDDIR)/$(1)/$(NAME):
	$$(MAKE) TARGETS=$(1) all
endif
endef
$(foreach p,apple2 atari atmos c64 c64.chr plus4 cx16 rp6502,$(eval $(call cc65_bin_stub,$(p))))

ifeq ($(words $(TARGETLIST)),1)
ifeq ($($(TARGETLIST)_FAMILY),cc65)
ifeq ($($(TARGETLIST)_AVAILABLE),1)

PROGRAM      := $(BUILDDIR)/$(TARGETLIST)/$(NAME)
TARGETOBJDIR := $(OBJDIR)/$(TARGETLIST)
CC65TARGET   := $($(TARGETLIST)_CC65)
LDFLAGS      += $($(TARGETLIST)_LDFLAGS)

SOURCES := $(wildcard $(SRCDIR)/*.c)
SOURCES += $(wildcard $(SRCDIR)/*.s)
SOURCES += $(wildcard $(SRCDIR)/*.asm)
SOURCES += $(wildcard $(SRCDIR)/*.a65)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.c)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.s)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.asm)
SOURCES += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.a65)

OBJECTS := $(addsuffix .o,$(basename $(addprefix $(TARGETOBJDIR)/,$(notdir $(SOURCES)))))
DEPENDS := $(OBJECTS:.o=.d)

LIBS += $(wildcard $(SRCDIR)/*.lib)
LIBS += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.lib)

CONFIG += $(wildcard $(SRCDIR)/$(TARGETLIST)/*.cfg)
CONFIG += $(wildcard $(SRCDIR)/*.cfg)
ifneq ($(word 2,$(CONFIG)),)
  CONFIG := $(firstword $(CONFIG))
  $(info Using config file $(CONFIG) for linking)
endif

# --- abstract OPTIONS (optsize / optspeed / listing / mapfile / ...) ---

define _optspeed_
  CFLAGS += -Oris
endef

define _optsize_
  CFLAGS += -Or
endef

define _listing_
  CFLAGS += --listing $$(@:.o=.lst)
  ASFLAGS += --listing $$(@:.o=.lst)
  REMOVES += $(addsuffix .lst,$(basename $(OBJECTS)))
endef

define _mapfile_
  LDFLAGS += --mapfile $$@.map
  REMOVES += $(PROGRAM).map
endef

define _labelfile_
  LDFLAGS += -Ln $$@.lbl
  REMOVES += $(PROGRAM).lbl
endef

define _debugfile_
  LDFLAGS += -Wl --dbgfile,$$@.dbg
  REMOVES += $(PROGRAM).dbg
endef

-include $(DEPENDS)
-include $(STATEFILE)

ifeq ($(origin OPTIONS),command line)
  ifneq ($(OPTIONS),$(_OPTIONS_))
    ifeq ($(OPTIONS),)
      $(info Removing OPTIONS)
      $(shell $(RM) $(STATEFILE))
      $(eval $(STATEFILE):)
    else
      $(info Saving OPTIONS=$(OPTIONS))
      $(shell mkdir -p $(BUILDDIR) && echo _OPTIONS_=$(OPTIONS) > $(STATEFILE))
    endif
    $(eval $(OBJECTS): $(STATEFILE))
  endif
else
  ifeq ($(origin _OPTIONS_),file)
    $(info Using saved OPTIONS=$(_OPTIONS_))
    OPTIONS = $(_OPTIONS_)
    $(eval $(OBJECTS): $(STATEFILE))
  endif
endif

ifeq ($(OPTIONS),)
  OPTIONS := optsize
endif

$(foreach o,$(subst $(COMMA),$(SPACE),$(OPTIONS)),$(eval $(_$o_)))

# --- compile / link ---

$(TARGETOBJDIR):
	$(call MKDIR,$@)

$(BUILDDIR)/$(TARGETLIST):
	$(call MKDIR,$@)

vpath %.c $(SRCDIR)/$(TARGETLIST) $(SRCDIR)

$(TARGETOBJDIR)/%.o: %.c | $(TARGETOBJDIR)
	cl65 -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(CFLAGS) -o $@ $<

vpath %.s $(SRCDIR)/$(TARGETLIST) $(SRCDIR)

$(TARGETOBJDIR)/%.o: %.s | $(TARGETOBJDIR)
	cl65 -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(ASFLAGS) -o $@ $<

vpath %.asm $(SRCDIR)/$(TARGETLIST) $(SRCDIR)

$(TARGETOBJDIR)/%.o: %.asm | $(TARGETOBJDIR)
	cl65 -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(ASFLAGS) -o $@ $<

vpath %.a65 $(SRCDIR)/$(TARGETLIST) $(SRCDIR)

$(TARGETOBJDIR)/%.o: %.a65 | $(TARGETOBJDIR)
	cl65 -t $(CC65TARGET) -c --create-dep $(@:.o=.d) $(ASFLAGS) -o $@ $<

$(PROGRAM): $(CONFIG) $(OBJECTS) $(LIBS) | $(BUILDDIR)/$(TARGETLIST)
	cl65 -t $(CC65TARGET) $(LDFLAGS) -o $@ $(patsubst %.cfg,-C %.cfg,$^)

all: $(PROGRAM)

clean:
	$(call RMFILES,$(OBJECTS))
	$(call RMFILES,$(DEPENDS))
	$(call RMFILES,$(REMOVES))
	$(call RMFILES,$(PROGRAM))

endif # available
endif # family cc65
endif # single target
