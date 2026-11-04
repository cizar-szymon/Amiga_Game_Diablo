#!/bin/sh
# Like scene_dungeon.sh, Tab 4.3 s later: with sound the level loads 4.3 s longer (effects from the MPQ).
# Fixed measurement scene: hero AR (save/single_1.sv, dungeon level 2,
# fight with skeletons), 25 s standing, then quit; prints the stats lines.
#   sh tools/scene_dungeon.sh run-prof|run-blitc [label]
set -e
cd "$(dirname "$0")/.."
test -f build/amiga/devilutionx || { echo "no build/amiga/devilutionx"; exit 1; }
R="python tools/amiga_remote.py"
sh tools/restore_saves.sh
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx-test >/dev/null
$R put tools/amiga/$1 RAM:$1 >/dev/null
$R start "Execute RAM:$1" >/dev/null
python -c "import time; time.sleep(8)"
sh tools/cmd.sh "0 click 320 213" "2500 click 410 270" "500 click 300 445" "2500 key return" "29300 key tab" "1000 mark measure" "0 profon" "20000 profoff"
python -c "import time; time.sleep(67)"
$R stop devilutionx-test >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/scene-${2:-$1}.log >/dev/null
grep -A6 "mark: measure" build/amiga/scene-${2:-$1}.log | grep stats: | tail -4
