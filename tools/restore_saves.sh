#!/bin/sh
# The game rewrites the save when it quits: every test starts from the same
# saved state (WARP: single_0.sv.bak in town, AR: single_1.sv.bak in the dungeon).
cd "$(dirname "$0")/.."
R="python tools/amiga_remote.py"
$R run "Copy WorkSD:Games/DiabloWarp/save/single_0.sv.bak WorkSD:Games/DiabloWarp/save/single_0.sv" >/dev/null
$R run "Copy WorkSD:Games/DiabloWarp/save/single_1.sv.bak WorkSD:Games/DiabloWarp/save/single_1.sv" >/dev/null
