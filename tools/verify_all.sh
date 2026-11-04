#!/bin/sh
# All renderer checks (scene, game copy, card) in town and dungeon + memory check.
#   sh tools/verify_all.sh label
cd "$(dirname "$0")/.."
RUNSCRIPT=run-vall sh tools/verify_town.sh "$1-town" >/dev/null
RUNSCRIPT=run-vall sh tools/verify_render.sh "$1-dungeon" >/dev/null
for f in build/amiga/vtown-$1-town.log build/amiga/verify-$1-dungeon.log; do
  echo "== $f"
  grep -E "VERIFY|GUARD|TOP WRITE|GAME COPY|verify [0-9]+ frames" "$f" | head -8
done
echo "== Avail"
timeout 30 python tools/amiga_remote.py run "Avail" | head -3 || echo "AVAIL FAILED"
