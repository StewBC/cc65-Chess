#!/bin/sh
# Keep emulator CMOS, configuration and HostFS writes inside build/archie.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ARCU_HOME=${ARCU_HOME:-"$HOME/Applications/arculator"}
DEST="$ROOT/build/archie/arculator"
if [ ! -x "$ARCU_HOME/arculator" ]; then
	echo "Arculator not found at $ARCU_HOME (set ARCU_HOME)" >&2
	exit 1
fi
mkdir -p "$DEST/configs" "$DEST/hostfs"
cp "$ARCU_HOME/arculator" "$DEST/arculator"
for dir in lib roms podules ddnoise; do
	if [ ! -e "$DEST/$dir" ]; then ln -s "$ARCU_HOME/$dir" "$DEST/$dir"; fi
done
if [ ! -d "$DEST/cmos" ]; then cp -R "$ARCU_HOME/cmos" "$DEST/cmos"; fi
if [ ! -f "$DEST/configs/A3010.cfg" ]; then
	cp "$ARCU_HOME/configs/A3010.cfg" "$DEST/configs/A3010.cfg"
fi
if [ ! -f "$DEST/arc.cfg" ]; then cp "$ARCU_HOME/arc.cfg" "$DEST/arc.cfg"; fi
cp -R "$ROOT/build/archie/!Chess" "$DEST/hostfs/"
echo 'Open HostFS and double-click !Chess. Ctrl-End releases the emulator mouse.'
if [ "$(uname -s)" = Darwin ]; then
	# A bundle gives LaunchServices and UI tools an identity for the
	# private emulator.  Its writable data still lives in DEST.
	python3 - "$DEST" <<'PY'
from pathlib import Path
import plistlib
import shutil
import sys
base = Path(sys.argv[1])
app = base / 'Chess Arculator.app'
mac = app / 'Contents/MacOS'
mac.mkdir(parents=True, exist_ok=True)
shutil.copy2(base / 'arculator', mac / 'arculator')
for name in ['lib', 'roms', 'podules', 'ddnoise', 'cmos', 'configs', 'hostfs']:
    dest = mac / name
    if not dest.exists():
        dest.symlink_to(base / name, target_is_directory=True)
if not (mac / 'arc.cfg').exists():
    shutil.copy2(base / 'arc.cfg', mac / 'arc.cfg')
info = {'CFBundleIdentifier': 'local.cc65chess.arculator',
        'CFBundleName': 'Chess Arculator', 'CFBundleExecutable': 'arculator',
        'CFBundlePackageType': 'APPL', 'CFBundleVersion': '1'}
(app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
PY
	exec open -W "$DEST/Chess Arculator.app" --args A3010
fi
cd "$DEST"
exec ./arculator A3010
