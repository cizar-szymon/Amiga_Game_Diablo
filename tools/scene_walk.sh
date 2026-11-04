#!/bin/sh
# Sustained walking (left button held towards a screen point) in town (hero
# WARP) or the dungeon (hero AR): 4 directions x 5 s; prints stats, cells and
# check lines, then Avail.
#   sh tools/scene_walk.sh town|dungeon run-script label
cd "$(dirname "$0")/.."
test -f build/amiga/devilutionx || { echo "no build/amiga/devilutionx"; exit 1; }
R="python tools/amiga_remote.py"
sh tools/restore_saves.sh
case "$1" in
  town) HERO="2500 click 410 296"; MAP="22000 mark start" ;;
  *) HERO="2500 click 410 270"; MAP="25000 key tab" ;;
esac
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx-test >/dev/null
$R put tools/amiga/$2 RAM:$2 >/dev/null
$R start "Execute RAM:$2" >/dev/null
python -c "import time; time.sleep(8)"
sh tools/cmd.sh "0 click 320 213" "$HERO" "500 click 300 445" "2500 key return" "$MAP" \
  "2000 mark walk" "0 profon" "0 press 560 300" "5000 release" "0 press 80 60" "5000 release" \
  "0 press 560 60" "5000 release" "0 press 80 300" "5000 release" "500 mark end" "0 profoff"
python -c "import time; time.sleep(62)"
$R stop devilutionx-test >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/walk-$3.log >/dev/null
sed -n '/mark: walk/,/mark: end/p' build/amiga/walk-$3.log | grep -E "stats:|cells: [0-9]|VERIFY|GUARD|GAME COPY|TOP WRITE" | head -20
echo "== Avail"
timeout 30 $R run "Avail" | head -3 || echo "AVAIL FAILED"
