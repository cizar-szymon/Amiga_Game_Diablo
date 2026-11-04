#!/bin/sh
# Fixed measurement scene: hero AR (save/single_1.sv, dungeon level 2,
# fight with skeletons), 25 s standing, then quit; prints the stats lines.
#   sh tools/scene_dungeon.sh run-prof|run-blitc [label]
set -e
cd "$(dirname "$0")/.."
test -f build/amiga/devilutionx || { echo "no build/amiga/devilutionx"; exit 1; }
R="python tools/amiga_remote.py"
$R put tools/amiga/$1 WorkSD:Games/DiabloWarp/$1 >/dev/null
$R start "Execute WorkSD:Games/DiabloWarp/$1" >/dev/null
python -c "import time; time.sleep(8)"
sh tools/cmd.sh "0 click 320 213" "2500 click 410 270" "500 click 300 445" "2500 key return" "25000 key tab" "1000 mark measure"
python -c "import time; time.sleep(62)"
$R stop devilutionx >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/scene-${2:-$1}.log >/dev/null
grep -A6 "mark: measure" build/amiga/scene-${2:-$1}.log | grep stats: | tail -4
