#!/bin/sh
# Screenshot of the game's 8-bit screen with the right palette (clutshot):
# sh tools/shot.sh out.png
set -e
cd "$(dirname "$0")/.."
R="python tools/amiga_remote.py"
$R put tools/amiga/bin/clutshot RAM:clutshot >/dev/null
$R run "RAM:clutshot" >/dev/null
$R get RAM:clutshot.ppm build/clutshot.ppm >/dev/null
python -c "from PIL import Image; import sys; Image.open('build/clutshot.ppm').save(sys.argv[1])" "$1"
echo "$1"
