#!/bin/sh
# Sends test input lines to the running game (RAM:diablo-cmd, see
# port/sdl/src/msdl_events.c):  sh tools/cmd.sh "0 key esc" "500 click 320 213"
set -e
cd "$(dirname "$0")/.."
printf '%s\n' "$@" > build/diablo-cmd
python tools/amiga_remote.py put build/diablo-cmd RAM:diablo-cmd >/dev/null
