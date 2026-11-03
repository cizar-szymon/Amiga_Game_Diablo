#!/bin/sh
# Reference build: upstream DevilutionX exactly as its Amiga CI builds it (real SDL2).
# Sources are copied into a Docker volume (fast Linux FS); result -> build/ref/devilutionx
# Usage: sh tools/build_ref.sh [clean]
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd -W 2>/dev/null || pwd)
IMG=amigadev/crosstools:m68k-amigaos-gcc10
VOL=diablo-ref-build
[ "$1" = clean ] && docker volume rm -f $VOL >/dev/null
mkdir -p build/ref
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "$ROOT/upstream/devilutionX:/src:ro" \
  -v $VOL:/work \
  -v "$ROOT/build/ref:/out" \
  $IMG sh -c '
    set -e
    mkdir -p /work/src && cp -ru /src/. /work/src/
    cd /work/src
    [ -f /work/build/build.ninja ] || cmake -S . -B /work/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DM68K_COMMON="-s -fbbb=- -ffast-math" \
        -DM68K_CPU=68040 -DM68K_FPU=hard
    cmake --build /work/build -j $(nproc) 2>&1 | tail -40
    cp /work/build/devilutionx /out/
    ls -l /out/devilutionx'
