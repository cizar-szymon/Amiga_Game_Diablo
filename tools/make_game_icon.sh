#!/bin/sh
# The game's Workbench icon: the DIABLO logo (tools/icon/diablo_logo.png, from
# the main menu, 104x40), stack 1000000, Tool Types (port/sdl/src/amiga_wb.c).
#   sh tools/make_game_icon.sh [out.info]
cd "$(dirname "$0")/.."
python tools/make_icon.py "${1:-build/amiga/devilutionx.info}" --frame tools/icon/diablo_logo.png --crop 0 0 104 40 \
  --lit-frame --stack 1000000 --tooltypes "DATADIR=WorkSD:Games/Diablo" "SAVEDIR=PROGDIR:save" "LANGUAGE=pl" \
  "(LANGUAGE=en)" "(LANGUAGE=de)" "SHOWFPS=YES" "(SKIPINTRO)" "(NOSOUND)"
