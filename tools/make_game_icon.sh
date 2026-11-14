#!/bin/sh
# The game's Workbench icon: the DIABLO logo (tools/icon/diablo_logo.png, from
# the main menu, 104x40), stack 1000000, Tool Types (port/sdl/src/amiga_wb.c).
#   sh tools/make_game_icon.sh [out.info]
cd "$(dirname "$0")/.."
python tools/make_icon.py "${1:-build/amiga/devilutionx.info}" --frame tools/icon/diablo_logo.png --crop 0 0 104 40 \
  --lit-frame --stack 1000000 --tooltypes "DATADIR=PROGDIR:data" "SAVEDIR=PROGDIR:save" "LANGUAGE=pl" \
  "(LANGUAGE=en)" "(LANGUAGE=de)" "SHOWFPS=YES" "GAME=DIABLO" "(SKIPINTRO)" "(NOSOUND)"
# the second icon: Diablo: Hellfire (a project started by devilutionx, GAME=HELLFIRE)
D=$(dirname "${1:-build/amiga/devilutionx.info}")
printf 'Double-click the Hellfire icon: DiabloWarp in Diablo: Hellfire mode.\nIkona Hellfire: DiabloWarp w trybie Diablo: Hellfire.\n' > "$D/Hellfire"
python tools/make_icon.py "$D/Hellfire.info" --frame tools/icon/hellfire_logo.png --crop 0 0 120 40 --lit-frame \
  --stack 1000000 --project devilutionx --tooltypes "GAME=HELLFIRE"
