#!/bin/sh
# Warp port build: clean upstream copy + port/apply_port.py + native SDL2
# subset (port/sdl) -> build/amiga/devilutionx (stripped, for the Amiga)
# and build/amiga/devilutionx.debug (symbols, for tools/prof.sh).
# Usage: sh tools/build.sh [clean] [extra cmake -D options...]
# The build tree lives in the Docker volume diablo-build (fast Linux FS).
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd -W 2>/dev/null || pwd)
IMG=amigadev/crosstools:m68k-amigaos-gcc10
VOL=diablo-build
if [ "$1" = clean ]; then
  docker volume rm -f $VOL >/dev/null
  shift
fi
EXTRA="$*"
# compiler flags (the toolchain file caches its own at the first configure;
# these replace them). No -s: symbols stay for devilutionx.debug.
CFLAGS68K="-fbbb=- -ffast-math -m68040 -mhard-float -fomit-frame-pointer -mcrt=nix20"
mkdir -p build/amiga
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "$ROOT/upstream/devilutionX:/upstream:ro" \
  -v "$ROOT/port:/port:ro" \
  -v $VOL:/work \
  -v "$ROOT/build/amiga:/out" \
  -e EXTRA="$EXTRA" -e CFLAGS68K="$CFLAGS68K" \
  $IMG sh -c '
    set -e
    # upstream copied once (clean to refresh); our changes re-applied every time
    if [ ! -d /work/src ]; then
      mkdir -p /work/src && (cd /upstream && tar cf - --exclude=.git .) | (cd /work/src && tar xf -)
    fi
    python3 /port/apply_port.py /work/src /upstream >/work/apply.log || { cat /work/apply.log; exit 1; }
    cd /work/src
    # configured every time (nothing is rebuilt when nothing changed)
    cmake -S . -B /work/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DM68K_COMMON="-fbbb=- -ffast-math" \
        -DCMAKE_C_FLAGS="$CFLAGS68K" -DCMAKE_CXX_FLAGS="$CFLAGS68K" \
        -DTOOLCHAIN_LDFLAGS="-Wl,-Map=/work/build/devilutionx.map" \
        -DM68K_CPU=68040 -DM68K_FPU=hard \
        -DNOSOUND=ON \
        -DDEVILUTIONX_SCREENSHOT_FORMAT=DEVILUTIONX_SCREENSHOT_FORMAT_PCX \
        -DSDL2_DIR=/port/sdl/cmake $EXTRA >/work/configure.log || { tail -30 /work/configure.log; exit 1; }
    cmake --build /work/build -j $(nproc) >/work/build.log 2>&1 || true
    grep -E "error|undefined|FAILED|Linking CXX executable" /work/build.log | head -60
    [ -f /work/build/devilutionx ] || exit 1
    cp /work/build/devilutionx /out/devilutionx.debug
    cp /work/build/devilutionx.map /out/ 2>/dev/null || true
    m68k-amigaos-strip -o /out/devilutionx /work/build/devilutionx
    ls -l /out/devilutionx /out/devilutionx.debug'
