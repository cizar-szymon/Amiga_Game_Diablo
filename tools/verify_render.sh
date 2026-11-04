#!/bin/sh
# Incremental renderer check in the dungeon (hero AR, level 2): stand, fight,
# walk, with ENV:DIABLO_VERIFY_RENDER; prints the verify and cells lines.
#   sh tools/verify_render.sh [label]
cd "$(dirname "$0")/.."
test -f build/amiga/devilutionx || { echo "no build/amiga/devilutionx"; exit 1; }
R="python tools/amiga_remote.py"
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx >/dev/null
$R put tools/amiga/${RUNSCRIPT:-run-vrender} WorkSD:Games/DiabloWarp/${RUNSCRIPT:-run-vrender} >/dev/null
$R start "Execute WorkSD:Games/DiabloWarp/${RUNSCRIPT:-run-vrender}" >/dev/null
python -c "import time; time.sleep(8)"
sh tools/cmd.sh "0 click 320 213" "2500 click 410 270" "500 click 300 445" "2500 key return" "25000 key tab" \
  "1000 mark vstand" "15000 mark vwalk" "0 click 180 230" "3000 click 320 300" "3000 click 140 200" \
  "3000 click 300 290" "3000 click 450 150" "3000 click 200 300" "3000 mark vend"
python -c "import time; time.sleep(72)"
sh tools/shot.sh build/amiga/verify-${1:-last}.png >/dev/null
$R stop devilutionx >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/verify-${1:-last}.log >/dev/null
grep -E "VERIFY|verify|mark" build/amiga/verify-${1:-last}.log | tail -25
echo "== Avail (memory list check)"; timeout 30 $R run "Avail" || echo "AVAIL FAILED - memory corrupted?"
