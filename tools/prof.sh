#!/bin/sh
# Fetch RAM:diablo-prof.txt from the Amiga and print the top functions.
#   sh tools/prof.sh [N]
# (profiler: "0 profon" / "0 profoff" through tools/cmd.sh, or SetEnv DIABLO_PROFILE 1)
# Symbols come from the linker map of the deployed build (tools/build.sh).
cd "$(dirname "$0")/.."
python tools/amiga_remote.py get RAM:diablo-prof.txt build/amiga/diablo-prof.txt >/dev/null || exit 1
python tools/map2syms.py build/amiga/devilutionx.map build/amiga/devilutionx.syms >/dev/null
python tools/prof.py build/amiga/diablo-prof.txt build/amiga/devilutionx.syms ${1:-40}
