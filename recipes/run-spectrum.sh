#!/bin/sh
# Launch the Spectrum build in ZEsarUX 48K.
#
# Prefer the .sna: it starts at USR with no tape and no CLEAR.
# The .tap is for LOAD "" / drop-on-window once ORG is 24000.
#
# `make spectrum test` calls this.  Do not use `open -a ZEsarUX file.tap`.
# On this Mac, .tap is "RetroArch Content File" and Launch Services
# refuses it.  Drop the file on an already-open ZEsarUX *window*, or
# call this script.

set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/spectrum"
BIN=/Applications/zesarux.app/Contents/MacOS/zesarux

if [ ! -x "$BIN" ]; then
	echo "ZEsarUX not found at $BIN" >&2
	exit 1
fi

# zesarux cds into Contents/MacOS, so a repo-relative path is not found.
# make spectrum test passes build/spectrum/chess.sna; make it absolute.
if [ -n "$1" ]; then
	case "$1" in
		/*) file="$1" ;;
		*)  file="$(CDPATH= cd -- "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
	esac
	if [ ! -f "$file" ]; then
		echo "no $file — run make spectrum first" >&2
		exit 1
	fi
	case "$file" in
		*.sna) exec "$BIN" --noconfigfile --machine 48k --snap "$file" ;;
		*.tap) exec "$BIN" --noconfigfile --machine 48k --fastautoload --tape "$file" ;;
		*) echo "usage: $0 [chess.sna|chess.tap]" >&2; exit 2 ;;
	esac
fi

SNA="$OUT/chess.sna"
TAP="$OUT/chess.tap"

if [ -f "$SNA" ]; then
	exec "$BIN" --noconfigfile --machine 48k --snap "$SNA"
fi

if [ ! -f "$TAP" ]; then
	echo "no $SNA or $TAP — run make spectrum first" >&2
	exit 1
fi

exec "$BIN" --noconfigfile --machine 48k --fastautoload --tape "$TAP"
