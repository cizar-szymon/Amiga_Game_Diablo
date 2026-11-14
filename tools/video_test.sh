#!/bin/sh
# Video decoder test on the Amiga: starts devilutionx-test in Hellfire and in
# Diablo mode, lets the start videos play, prints the "video" log lines
# (decoding time per frame, frames shown, checksum of all decoded frames).
# Needs the Hellfire MPQs in WorkSD:Games/DiabloWarp/data.
#   sh tools/video_test.sh [label]
set -e
cd "$(dirname "$0")/.."
R="python tools/amiga_remote.py"
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx-test >/dev/null
for g in HELLFIRE DIABLO; do
  sed "s/GAME=HELLFIRE/GAME=$g/" tools/amiga/run-hellfire > build/run-video
  $R put build/run-video RAM:run-video >/dev/null
  $R start "Execute RAM:run-video" >/dev/null
  python -c "import time; time.sleep(32)"
  $R stop devilutionx-test >/dev/null
  python -c "import time; time.sleep(5)"
  $R get RAM:diablo.log build/amiga/video-$g-${1:-test}.log >/dev/null
  echo "$g: $(grep -E 'INFO: video' build/amiga/video-$g-${1:-test}.log | head -1)"
done
