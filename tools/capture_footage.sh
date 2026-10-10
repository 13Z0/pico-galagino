#!/usr/bin/env bash
# Capture screenshots of the games for tools/mkartwork.py --footage.
#
#   tools/capture_footage.sh <romroot> <outdir>
#
# <romroot> holds the game folders as on the SD card (PACMAN, GALAGA, ...).
# Runs the host harness with Tate mode Off and writes the four frames
# mkartwork.py reads, as 320x240 PPM with a .png beside each to look at:
#
#   badge.ppm    theme 0 box art: the game menu
#   left.ppm     theme 1 left panel: Pac-Man
#   middle.ppm   theme 1 middle panel: Galaga
#   right.ppm    theme 1 right panel: Donkey Kong
#
# Copy another frame over a name to change what that part of the artwork shows.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -ne 2 ]; then
    sed -n 2,15p "$0"
    exit 1
fi
romroot=$1
out=$2

[ -x hosttest/gal_host ] || hosttest/build.sh
mkdir -p "$out/frames"

# shot <game|menu> <frame> <name> [harness options]
shot() {
    local game=$1 frame=$2 name=$3
    shift 3
    ./hosttest/gal_host "$romroot" "$game" $((frame + 1)) "$frame" "$out/frames" "$@" > /dev/null
    mv "$out/frames/frame_$(printf %05d "$frame").ppm" "$out/$name.ppm"
    python3 hosttest/ppm2png.py "$out/$name.ppm" > /dev/null
}

shot menu 1 badge
shot PACMAN 1100 left --coin 600 --start 700 --play 760
shot GALAGA 1400 middle --coin 700 --start 800 --play 900
shot DONKEYKONG 2300 right --coin 900 --start 1000 --play 1060
rm -rf "$out/frames"
echo "badge, left, middle and right.ppm in $out"
