#!/bin/bash
#
# vice-run.sh - run a C64 program headless under VICE and screenshot the result
#
# This is how every "on a real C64" number in the plan was measured.  Three
# things about it are not obvious and each took a while to work out:
#
#   1. VICE looks for its ROMs in ./data relative to the working directory, and
#      ignores VICE_DATADIR.  So the script works in a scratch directory with a
#      symlink called "data" pointing at the installed share/vice.
#
#   2. The macOS build needs GSETTINGS_SCHEMA_DIR and XDG_DATA_DIRS set by hand
#      when the binary is run directly rather than through the .app wrapper,
#      or GTK aborts on startup.
#
#   3. -warp does NOT distort the measurement.  The C64 jiffy clock counts
#      emulated time, so a program that times itself with clock() reports the
#      same number whether the emulator is running at 1x or 1000x.  Always use
#      it; the runs take minutes of emulated time.
#
# Use -ntsc, because cc65 defines CLOCKS_PER_SEC as 60 for the c64 target and
# that is only true on NTSC - the PAL jiffy ticks at about 50 Hz, so reading a
# PAL run as 60 Hz is a silent 20% error.
#
# Usage:  ./vice-run.sh <program.prg> [output.png] [cycle-limit]
#
# Read the resulting screenshot to get the numbers; the program prints them and
# then spins so the screen still holds them when the cycle limit fires.

set -e

PRG="${1:?usage: vice-run.sh <program.prg> [out.png] [cycles]}"
OUT="${2:-/tmp/vice-out.png}"
CYCLES="${3:-1200000000}"

VICE_APP="${VICE_APP:-/Applications/vice-arm64-gtk3-3.10/VICE.app/Contents/Resources}"
WORK="$(mktemp -d)"

if [ ! -x "$VICE_APP/bin/x64sc" ]; then
	echo "x64sc not found under $VICE_APP - set VICE_APP to your VICE Resources dir" >&2
	exit 1
fi

ln -sfn "$VICE_APP/share/vice" "$WORK/data"
cp "$PRG" "$WORK/"

export GSETTINGS_SCHEMA_DIR="$VICE_APP/share/glib-2.0/schemas"
export XDG_DATA_DIRS="$VICE_APP/share"

cd "$WORK"
"$VICE_APP/bin/x64sc" -ntsc -warp \
	-limitcycles "$CYCLES" \
	-autostart "$(basename "$PRG")" \
	-exitscreenshot "$OUT" >/dev/null 2>&1 || true

rm -rf "$WORK"
echo "screenshot: $OUT"
