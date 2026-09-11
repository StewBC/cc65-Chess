#!/bin/sh
# Launch the TI-99/4A build in Homebrew MAME with PEB 32K.
# `make ti99 test` calls this.  Lua jumps the cart so we do not sit
# on READY-PRESS ANY KEY TO BEGIN.

set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/ti99"
MAME=${MAME:-/opt/homebrew/bin/mame}
ROMS=${MAME_ROMS:-$HOME/.mame/roms}

if [ ! -x "$MAME" ]; then
	echo "mame not found at $MAME" >&2
	exit 1
fi

if [ -n "$1" ]; then
	case "$1" in
		/*) file="$1" ;;
		*)  file="$(CDPATH= cd -- "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
	esac
else
	file="$OUT/chess.rpk"
fi

if [ ! -f "$file" ]; then
	echo "no $file — run make ti99 first" >&2
	exit 1
fi

LUA="$OUT/boot.lua"
if [ ! -f "$LUA" ]; then
	LUA="$ROOT/src/ti99/boot.lua"
fi

CFGDIR="${MAME_CFG:-$HOME/.mame}"
mkdir -p "$CFGDIR/cfg" "$CFGDIR/nvram"

exec "$MAME" ti99_4a \
	-rompath "$ROMS" \
	-cfg_directory "$CFGDIR/cfg" \
	-nvram_directory "$CFGDIR/nvram" \
	-ioport peb -ioport:peb:slot2 32kmem \
	-cart "$file" \
	-autoboot_script "$LUA" \
	-window -nomaximize -skip_gameinfo
