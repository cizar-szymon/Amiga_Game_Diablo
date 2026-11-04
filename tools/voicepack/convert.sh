#!/bin/sh
# A DevilutionX voice pack (e.g. pl.mpq from devilutionx-assets, its speech as MP3)
# made into one the Warp port plays without decoding: every speech file the game
# knows (assets/txtdata/sound/effects*.tsv) taken out as .mp3, converted with ffmpeg
# to the original's format (WAV, 8 bit unsigned, mono, 22050 Hz) and packed again
# under its .wav name. The game looks for <language>.mpq in its MPQ search paths.
#   sh tools/voicepack/convert.sh private/voice/pl.mpq private/voice/pl-wav.mpq
set -e
cd "$(dirname "$0")/../.."
IN=$1; OUT=$2
W=build/voicepack
rm -rf $W && mkdir -p $W/mp3 $W/wav
# speech names from the game's tables, .wav -> .mp3
tail -q -n +2 upstream/devilutionX/assets/txtdata/sound/effects.tsv upstream/devilutionX/assets/txtdata/sound/effects-unused.tsv \
  | awk -F'\t' '{print $3}' | tr -d '\r' | sort -u > $W/names_wav.txt
sed 's/\.wav$/.mp3/' $W/names_wav.txt > $W/names_mp3.txt
INDIR=$(cd "$(dirname "$IN")" && (pwd -W 2>/dev/null || pwd))
MSYS_NO_PATHCONV=1 docker run --rm -v diablo-build:/work:ro -v "$(pwd -W):/p" -v "$INDIR:/in:ro" -w /p gcc:13 sh -c "
  gcc -O2 -w -I/work/build/_deps/mpqfs-src/include -I/work/build/_deps/mpqfs-src/src -o $W/mpqtool tools/voicepack/mpqtool.c \
    \$(ls /work/build/_deps/mpqfs-src/src/*.c) -lz &&
  $W/mpqtool extract /in/$(basename "$IN") $W/names_mp3.txt $W/mp3"
# MP3 -> WAV u8 mono 22050 (the original's format)
: > $W/names_out.txt
find $W/mp3 -name '*.mp3' | while read -r f; do
  rel=${f#$W/mp3/}
  out=$W/wav/${rel%.mp3}.wav
  mkdir -p "$(dirname "$out")"
  ffmpeg -loglevel error -y -i "$f" -ac 1 -ar 22050 -c:a pcm_u8 "$out"
  echo "${rel%.mp3}.wav" | tr '/' '\\' >> $W/names_out.txt
done
OUTDIR=$(cd "$(dirname "$OUT")" && (pwd -W 2>/dev/null || pwd))
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/p" -v "$OUTDIR:/out" -w /p gcc:13 sh -c "
  $W/mpqtool pack /out/$(basename "$OUT") $W/names_out.txt $W/wav"
ls -l "$OUT"
