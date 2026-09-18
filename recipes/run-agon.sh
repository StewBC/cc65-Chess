#!/bin/sh
# Launch the Agon build in Fab Agon Emulator.
#
# Copies chess.bin into the emulator's virtual SD card as a standalone
# program under /bin (load address $40000).  Do not put it in /mos —
# those are moslets, loaded at $B0000, and this binary is not one.
# Writes autoexec.txt so `make agon test` boots into the game.
# Set FAE_HOME if the emulator is not in the default install location.

set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
FAE_HOME="${FAE_HOME:-$HOME/.local/share/fab-agon-emulator-v1.2.5-macos-arm64}"
BIN="$FAE_HOME/fab-agon-emulator"

if [ -n "$1" ]; then
	case "$1" in
		/*) file="$1" ;;
		*)  file="$(CDPATH= cd -- "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
	esac
else
	file="$ROOT/build/agon/chess.bin"
fi

if [ ! -f "$file" ]; then
	echo "no $file — run make agon first" >&2
	exit 1
fi

if [ ! -x "$BIN" ]; then
	echo "Fab Agon emulator not found at $BIN" >&2
	echo "Set FAE_HOME to the emulator install directory." >&2
	exit 1
fi

mkdir -p "$FAE_HOME/sdcard/bin"
cp "$file" "$FAE_HOME/sdcard/bin/chess.bin"
rm -f "$FAE_HOME/sdcard/mos/chess.bin"
printf 'chess\n' > "$FAE_HOME/sdcard/autoexec.txt"

cd "$FAE_HOME"
exec ./fab-agon-emulator
