#!/bin/sh
# Boot the A500 port under the FS-UAE the user already launched once.
# Kickstart 1.3 and rom.key stay where the launcher put them.  The chess
# disk is the boot directory make amiga just wrote; there is no Workbench
# floppy.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
FS_UAE=${AMIGA_FS_UAE:-/Volumes/EXTERNAL/Applications/FS-UAE.app/Contents/MacOS/fs-uae}
KICK=${AMIGA_KICKSTART:-"$HOME/Documents/FS-UAE/Kickstarts/amiga-os-130.rom"}
BOOT="$ROOT/build/amiga/boot"
CFG="$ROOT/build/amiga/test.fs-uae"

if [ ! -x "$FS_UAE" ]; then
	echo "FS-UAE not found at $FS_UAE (set AMIGA_FS_UAE)" >&2
	exit 1
fi
if [ ! -f "$KICK" ]; then
	echo "Kickstart 1.3 not found at $KICK (set AMIGA_KICKSTART)" >&2
	exit 1
fi
if [ ! -x "$BOOT/a.exe" ]; then
	echo "no $BOOT/a.exe — run make amiga first" >&2
	exit 1
fi

cat > "$CFG" <<EOF
amiga_model = A500
kickstart_file = $KICK
kickstarts_dir = $(dirname "$KICK")
chip_memory = 512
slow_memory = 512
hard_drive_0 = $BOOT
fullscreen = 0
keep_aspect = 1
automatic_input_grab = 0
# otherwise the cursor keys drive joystick port 1 and never reach the Amiga
joystick_port_1 = nothing
EOF

exec "$FS_UAE" "$CFG"
