#!/bin/sh
# FindPath equivalence: upstream path.cpp vs the Warp port's changed one
# (port/apply_port.py), compiled for the PC (Docker gcc:13), 16000 searches on
# random maps; both lines must show the same hash.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/pathtest
cp tools/pathtest/test_main.cpp build/pathtest/
python - <<'PY'
import io, sys
sys.path.insert(0, "port")
import apply_port
src = io.open("upstream/devilutionX/Source/engine/path.cpp", encoding="utf-8", newline="").read()
crlf = "\r\n" in src
new = src
for rel, old, rep in apply_port.CHANGES:
    if rel == "Source/engine/path.cpp":
        o = old.replace("\n", "\r\n") if crlf else old
        n = rep.replace("\n", "\r\n") if crlf else rep
        assert new.count(o) == 1
        new = new.replace(o, n)
io.open("build/pathtest/path_old.cpp", "w", encoding="utf-8", newline="").write(src)
io.open("build/pathtest/path_new.cpp", "w", encoding="utf-8", newline="").write(new.replace("#ifdef __amigaos__", "#if 1"))
PY
cd build/pathtest
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/t" -v "$(cd ../../upstream/devilutionX && (pwd -W 2>/dev/null || pwd)):/up:ro" -w /t gcc:13 sh -c '
  for v in old new; do g++ -std=c++23 -O2 -I/up/Source -I/up/3rdParty/tl -o t_$v test_main.cpp path_$v.cpp /up/Source/crawl.cpp; done
  ./t_old; ./t_new'
