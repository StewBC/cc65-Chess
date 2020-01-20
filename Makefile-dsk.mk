DSK = cc65-Chess.dsk

# For this one, see https://applecommander.github.io/
AC ?= ac.jar

REMOVES += $(DSK)

.PHONY: dsk
dsk: $(DSK)

$(DSK): cc65-Chess.apple2
	copy apple2\template.dsk $@
	java -jar $(AC) -p  $@ chess.system sys < $(shell cl65 --print-target-path)\apple2\util\loader.system
	java -jar $(AC) -as $@ chess        bin < cc65-Chess.apple2
