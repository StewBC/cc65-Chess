#!/bin/sh
# Launch the Color Computer 3 build in XRoar.
# `make coco3 test` calls this.  XRoar -run loads the DECB binary.

set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/coco3"

XROAR=${XROAR:-$HOME/.local/share/xroar-1.12-macosx/XRoar.app/Contents/MacOS/xroar}
if [ ! -x "$XROAR" ]; then
	XROAR=$(command -v xroar || true)
fi
if [ -z "$XROAR" ] || [ ! -x "$XROAR" ]; then
	echo "xroar not found (set XROAR= or install XRoar 1.12)" >&2
	exit 1
fi

ROMS=${XROAR_ROMS:-$HOME/Library/XRoar/roms}

if [ -n "$1" ]; then
	case "$1" in
		/*) file="$1" ;;
		*)  file="$(CDPATH= cd -- "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
	esac
else
	file="$OUT/chess.bin"
fi

if [ ! -f "$file" ]; then
	echo "no $file — run make coco3 first" >&2
	exit 1
fi

exec "$XROAR" -machine coco3 \
	-rompath "$ROMS" \
	-run "$file" \
	-ao null
