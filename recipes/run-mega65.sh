#!/bin/sh
set -eu
XEMU_MEGA65=${XEMU_MEGA65:-/Volumes/EXTERNAL/ext-dev/xemu/build/bin/xmega65.native}
MEGA65_ROM=${MEGA65_ROM:-/Volumes/EXTERNAL/ext-dev/xemu/rom/MEGA65.ROM}
case "$1" in
	/*) program=$1 ;;
	*) program=$(pwd)/$1 ;;
esac
exec "$XEMU_MEGA65" -rom "$MEGA65_ROM" -prg "$program" -prgmode 65
