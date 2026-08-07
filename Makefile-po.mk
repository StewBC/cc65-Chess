NAME = chess
PO = $(NAME).po

CA ?= cadius

# 1. ROBUST SHELL DETECTION
# Check if GNU Make is routing commands through a Unix shell (like sh.exe or bash)
ifneq ($(findstring sh,$(SHELL)),)
    # Unix Shell environment (Linux, macOS, WSL, or Windows via Git Bash / MSYS / sh.exe)
    CP = cp $1
    MV = mv
    RM = rm -f
    # Set this to stop Unix shells on Windows from mangling ProDOS paths
    NO_CONV = MSYS_NO_PATHCONV=1
else
    # Pure Windows Native Shell (CMD or PowerShell without sh.exe in %PATH%)
    CP = copy $(subst /,\,$1)
    MV = ren
    RM = del /Q
    NO_CONV =
endif

REMOVES += $(PO)

.PHONY: po
po: $(PO)

$(NAME).system:
	$(call CP, $(subst \,/,$(shell cl65 --print-target-path)/apple2/util/loader.system) $(NAME).system#FF2000)

$(PO): $(PROGRAM).apple2 $(NAME).system
	$(call CP, apple2/template.po $@)
	$(call CP, $(PROGRAM).apple2 $(NAME)#064000)
	# Always use an explicit forward slash '/' for ProDOS.
	$(NO_CONV) $(CA) addfile $(NAME).po /$(subst -,.,$(PROGRAM)) $(NAME).system#FF2000
	$(NO_CONV) $(CA) addfile $(NAME).po /$(subst -,.,$(PROGRAM)) $(NAME)#064000
	$(RM) $(NAME).system#FF2000
	$(RM) $(NAME)#064000
