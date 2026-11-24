#!/bin/sh
# Automap scene: hero AR (third in the list; save/single_1.sv, dungeon level 2; the save has the
# automap open): 15 s standing with the map, 15 s walking with the map, map
# off (Tab), 15 s standing, 15 s walking. Prints the stats lines per part.
#   sh tools/scene_automap.sh run-script label [prof]
#   prof / profwalk: the profiler runs while standing / walking with the map
cd "$(dirname "$0")/.."
BIN=${BIN:-build/amiga/devilutionx}
test -f $BIN || { echo "no $BIN"; exit 1; }
R="python tools/amiga_remote.py"
sh tools/restore_saves.sh
$R put $BIN WorkSD:Games/DiabloWarp/devilutionx-test >/dev/null
$R put tools/amiga/$1 RAM:$1 >/dev/null
$R start "Execute RAM:$1" >/dev/null
python -c "import time; time.sleep(8)"
PON="0 mark p"; POFF="0 mark p"; WON="0 mark p"; WOFF="0 mark p"
if [ "$3" = prof ]; then PON="0 profon"; POFF="0 profoff"; fi
if [ "$3" = profwalk ]; then WON="0 profon"; WOFF="0 profoff"; fi
sh tools/cmd.sh "0 click 320 213" "2500 click 410 323" "500 click 300 445" "2500 key return" \
  "32000 mark map-stand" "$PON" "15000 mark map-walk" "$POFF" "$WON" \
  "0 press 560 300" "4000 release" "0 press 80 60" "4000 release" "0 press 560 60" "4000 release" "0 press 80 300" "3000 release" "$WOFF" \
  "500 key tab" "2000 mark nomap-stand" "15000 mark nomap-walk" \
  "0 press 560 300" "4000 release" "0 press 80 60" "4000 release" "0 press 560 60" "4000 release" "0 press 80 300" "3000 release" \
  "500 mark end"
python -c "import time; time.sleep(115)"
$R stop devilutionx-test >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/automap-$2.log >/dev/null
for p in map-stand map-walk nomap-stand nomap-walk; do
  echo "== $p"
  sed -n "/mark: $p/,/mark: \(map\|nomap\|end\)/p" build/amiga/automap-$2.log | grep -E "stats:|VERIFY|GUARD|GAME COPY|TOP WRITE" | head -4
done
echo "== Avail"
timeout 30 $R run "Avail" | head -3 || echo "AVAIL FAILED"
