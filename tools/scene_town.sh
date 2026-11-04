#!/bin/sh
# Fixed measurement scene in town (hero WARP): 15 s standing, then walking
# up-right / down-left; prints the stats lines of both parts.
#   sh tools/scene_town.sh run-prof|run-full [label]
set -e
cd "$(dirname "$0")/.."
test -f build/amiga/devilutionx || { echo "no build/amiga/devilutionx"; exit 1; }
R="python tools/amiga_remote.py"
sh tools/restore_saves.sh
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx >/dev/null
$R put tools/amiga/$1 WorkSD:Games/DiabloWarp/$1 >/dev/null
$R start "Execute WorkSD:Games/DiabloWarp/$1" >/dev/null
python -c "import time; time.sleep(8)"
sh tools/cmd.sh "0 click 320 213" "2500 click 410 296" "500 click 300 445" "2500 key return" \
  "22000 mark stand" "16000 mark walk" "0 click 80 300" "3000 click 560 60" "3000 click 80 300" \
  "3000 click 560 60" "3000 click 80 300" "3000 click 560 60" "3000 mark end"
python -c "import time; time.sleep(70)"
$R stop devilutionx >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/town-${2:-$1}.log >/dev/null
sed -n '/mark: stand/,/mark: end/p' build/amiga/town-${2:-$1}.log | grep -E "stats:|mark" | grep -v "longest [0-9][0-9][0-9][0-9]"
