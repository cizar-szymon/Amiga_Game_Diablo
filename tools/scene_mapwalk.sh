#!/bin/sh
# Walking A/B with and without the automap: hero AR (third in the list,
# save/single_1.sv, dungeon level 2, the save has the map open), the same
# walk (4 directions) from the same place; "nomap" closes the map first.
# HEROY=349: hero WARP in town (single_0.sv, no map open: "nomap" opens it).
#   sh tools/scene_mapwalk.sh run-script label map|nomap [prof]
cd "$(dirname "$0")/.."
BIN=${BIN:-build/amiga/devilutionx}
test -f $BIN || { echo "no $BIN"; exit 1; }
R="python tools/amiga_remote.py"
sh tools/restore_saves.sh
$R put $BIN WorkSD:Games/DiabloWarp/devilutionx-test >/dev/null
$R put tools/amiga/$1 RAM:$1 >/dev/null
$R start "Execute RAM:$1" >/dev/null
python -c "import time; time.sleep(8)"
TAB="0 mark map"; [ "$3" = nomap ] && TAB="0 key tab"
PON="0 mark p"; POFF="0 mark p"; [ "$4" = prof ] && { PON="0 profon"; POFF="0 profoff"; }
sh tools/cmd.sh "0 click 320 213" "2500 click 410 ${HEROY:-323}" "500 click 300 445" "2500 key return" \
  "32000 mark loaded" "$TAB" "1000 mark walk" "$PON" \
  "0 press 560 300" "5000 release" "0 press 80 60" "5000 release" "0 press 560 60" "5000 release" "0 press 80 300" "5000 release" \
  "0 press 560 300" "5000 release" "0 press 80 60" "5000 release" "$POFF" "500 mark end"
python -c "import time; time.sleep(80)"
$R stop devilutionx-test >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/mapwalk-$2.log >/dev/null
sed -n "/mark: walk/,/mark: end/p" build/amiga/mapwalk-$2.log | grep -E "stats: [0-9]|cells: [0-9a]|VERIFY|GUARD|GAME COPY|TOP WRITE"
