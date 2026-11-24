#!/bin/sh
# Cleanup test (PORTING_PLAN "Sprzątanie po wyjściu"): the game started and
# quit several times in different ways; Avail FLUSH, titles and Status before
# and after every round. The memory difference must be 0.
#   sh tools/cleanup_test.sh [rounds-list]   e.g. "menu-quit game-quit game-ctrlc"
# Rounds: menu-quit (main menu, SDL_QUIT), game-quit (hero AR in the dungeon,
# map on, walk, SDL_QUIT), game-ctrlc (the same, Ctrl-C from AmiAgent stop).
# The log goes to WorkSD:Games/DiabloWarp/clean-test.log (not RAM:, it would count).
cd "$(dirname "$0")/.."
R="python tools/amiga_remote.py"
ROUNDS=${1:-"menu-quit menu-quit game-quit game-ctrlc game-quit game-ctrlc menu-quit"}
OUT=build/amiga/cleanup-test.txt
: > $OUT
snap() {
  echo "== $1" | tee -a $OUT
  $R run "Avail FLUSH" | grep -E "^(chip|fast|total)" | tee -a $OUT
  $R titles | sed 's/^/titles: /' | tee -a $OUT
  $R run "Status" | grep -v "Status\|^\[rc" | sed 's/^/status: /' | tee -a $OUT
}
gone() {
  for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    $R run "Status COM=devilutionx-test" | grep -q "^ *[0-9]" || return 0
    python -c "import time; time.sleep(2)"
  done
  echo "STILL RUNNING" | tee -a $OUT
}
$R put build/amiga/devilutionx WorkSD:Games/DiabloWarp/devilutionx-test >/dev/null
$R put tools/amiga/run-clean RAM:run-clean >/dev/null
$R run "Delete RAM:diablo-cmd QUIET" >/dev/null
snap "before"
n=0
for r in $ROUNDS; do
  n=$((n+1))
  sh tools/restore_saves.sh
  $R start "Execute RAM:run-clean" >/dev/null
  python -c "import time; time.sleep(9)"
  case $r in
    menu-quit) sh tools/cmd.sh "500 quit" ;;
    game-*)
      sh tools/cmd.sh "0 click 320 213" "2500 click 410 323" "500 click 300 445" "2500 key return" \
        "30000 key tab" "500 key tab" "500 press 560 300" "3000 release" "500 key tab" "1000 mark ok"
      python -c "import time; time.sleep(42)"
      if [ $r = game-quit ]; then sh tools/cmd.sh "0 quit"; else $R stop devilutionx-test >/dev/null; fi ;;
  esac
  gone
  python -c "import time; time.sleep(3)"
  snap "after round $n ($r)"
done
$R run "Delete RAM:run-clean RAM:diablo-cmd QUIET" >/dev/null
sh tools/restore_saves.sh
echo "== summary (fast / chip available)"
grep -E "^==|^fast|^chip" $OUT | paste - - - | awk '{print $0}'
