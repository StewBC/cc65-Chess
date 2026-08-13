#!/bin/sh
# Build the retained exact-cycle profiler into a bootable Apple II ProDOS image.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
output=${1:-/tmp/cc65-chess-profile.po}
case "$output" in
	/*) ;;
	*) output="$PWD/$output" ;;
esac
case "$output" in
	*.po) map=${output%.po}.map ;;
	*) echo "output must end in .po" >&2; exit 2 ;;
esac

profile_tmp_dir=$(mktemp -d /tmp/cc65-chess-a2m-profile.XXXXXX)
trap 'rm -rf -- "$profile_tmp_dir"' EXIT HUP INT TERM

cd "$repo_dir/tests"
cl65 -t apple2 -Or -I../src -I. -DSEARCH_PROFILE -DPROFILE_EXTERNAL_CLOCK \
	-Wl --mapfile,"$map" \
	-Wl -u,_gc_profileMarker -Wl -u,_gc_profileAck \
	--start-addr 0x4000 -Wl -D -Wl __HIMEM__=0xBF00 \
	-o "$profile_tmp_dir/chess" -C ../src/apple2/chessA2.cfg \
	../src/engine.c ../src/eval.c ../src/search.c c64profile.c

cp "$repo_dir/apple2/template.po" "$output"
cp "$(cl65 --print-target-path)/apple2/util/loader.system" \
	"$profile_tmp_dir/chess.system#FF2000"
cp "$profile_tmp_dir/chess" "$profile_tmp_dir/chess#064000"
cadius addfile "$output" /cc65.Chess "$profile_tmp_dir/chess.system#FF2000"
cadius addfile "$output" /cc65.Chess "$profile_tmp_dir/chess#064000"

marker=$(awk '{ for(i = 1; i < NF; ++i) if($i == "_gc_profileMarker" && length($(i+1)) == 6 && $(i+1) ~ /^[[:xdigit:]]+$/) { print $(i+1); exit } }' "$map")
ack=$(awk '{ for(i = 1; i < NF; ++i) if($i == "_gc_profileAck" && length($(i+1)) == 6 && $(i+1) ~ /^[[:xdigit:]]+$/) { print $(i+1); exit } }' "$map")
if [ -z "$marker" ] || [ -z "$ack" ]; then
	echo "profile handshake symbols missing from $map" >&2
	exit 1
fi

echo "image:  $output"
echo "map:    $map"
echo "marker: 0x$marker"
echo "ack:    0x$ack"
echo "run: python3 -u $repo_dir/tests/profile-run-a2m.py $output --marker 0x$marker --ack 0x$ack"
