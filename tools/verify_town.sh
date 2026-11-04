#!/bin/sh
# Incremental renderer check in town (hero WARP): stand, then walk up-right and
# down-left, with ENV:DIABLO_VERIFY_RENDER; prints the verify lines and checks
# the memory list afterwards (Avail).
#   sh tools/verify_town.sh [label]
cd "$(dirname "$0")/.."
test -f build/amiga/devilutionx || { echo "no build/amiga/devilutionx"; exit 1; }
R="python tools/amiga_remote.py"
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx >/dev/null
$R put tools/amiga/${RUNSCRIPT:-run-vrender} WorkSD:Games/DiabloWarp/${RUNSCRIPT:-run-vrender} >/dev/null
$R start "Execute WorkSD:Games/DiabloWarp/${RUNSCRIPT:-run-vrender}" >/dev/null
python -c "import time; time.sleep(8)"
sh tools/cmd.sh "0 click 320 213" "2500 click 410 296" "500 click 300 445" "2500 key return" \
  "22000 mark tstand" "8000 mark twalk" "0 click 80 300" "3000 click 560 60" "3000 click 80 300" \
  "3000 click 560 60" "3000 click 80 300" "3000 click 560 60" "3000 mark tend"
python -c "import time; time.sleep(70)"
sh tools/shot.sh build/amiga/vtown-${1:-last}.png >/dev/null
$R stop devilutionx >/dev/null
python -c "import time; time.sleep(5)"
$R get RAM:diablo.log build/amiga/vtown-${1:-last}.log >/dev/null
grep -E "VERIFY|verify|mark" build/amiga/vtown-${1:-last}.log | tail -25
echo "== Avail (memory list check)"
timeout 30 $R run "Avail" || echo "AVAIL FAILED - memory corrupted?"
