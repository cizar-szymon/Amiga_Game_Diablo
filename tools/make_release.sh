#!/bin/sh
# The Amiga release (after tools/build.sh), number from port/VERSION:
#   releases/amiga/DiabloWarp/            in git: the program and its icon,
#   releases/amiga/DiabloWarp.info        assets/ and mods/ (DevilutionX's own data),
#   releases/amiga/DiabloWarp-<v>.zip     doc/ (all texts: descriptions PL/EN, history,
#                                         LICENSE.md), empty data/ and save/ with a ReadMe
# No game data (diabdat.mpq, Blizzard) and no voice pack (pl.mpq) - the script refuses
# to make a release that has any .mpq or .sv in it.
#   private/DiabloWarp-<v>-full.zip      only on the PC: + data/diabdat.mpq and data/pl.mpq
# On the Amiga:  UnZip DiabloWarp-<v>.zip -d Work:Games
#   tools/make_release.sh
set -e
cd "$(dirname "$0")/.."
V=$(cat port/VERSION)
D=docs/release/amiga
R=releases/amiga
P=$R/DiabloWarp

grep -a -q "\$VER: devilutionx $V " build/amiga/devilutionx || { echo "build/amiga/devilutionx is not $V: tools/build.sh first" >&2; exit 1; }

rm -rf "$P"
mkdir -p "$P/data" "$P/save" "$P/doc"
cp build/amiga/devilutionx "$P/devilutionx"
# DevilutionX's own data (translations, fonts, tables, Lua) from the build tree
MSYS_NO_PATHCONV=1 docker run --rm -v diablo-build:/work:ro -v "$(pwd -W 2>/dev/null || pwd)/$P:/out" \
  amigadev/crosstools:m68k-amigaos-gcc10 sh -c 'cp -r /work/build/assets /work/build/mods /out/'
# Tool Types: the data and saves inside the drawer; the rest as hints in brackets
python tools/make_icon.py "$P/devilutionx.info" --frame tools/icon/diablo_logo.png --crop 0 0 104 40 --lit-frame \
  --stack 1000000 --pos 12 8 --tooltypes "DATADIR=PROGDIR:data" "SAVEDIR=PROGDIR:save" "(LANGUAGE=pl)" "(LANGUAGE=de)" \
  "(SHOWFPS=YES)" "(SKIPINTRO)" "(NOSOUND)" >/dev/null 2>&1
cp tools/icon/warp-drawer.info "$R/DiabloWarp.info"	# the game drawer: the WARP logo (tools/icon/warp-drawer.info, ../WARP-drawer.info)
# texts: Polish without diacritics (every Amiga shows it, as in the other ports)
for f in DiabloWarp.txt DiabloWarp_en.txt DiabloWarp_release.txt DiabloWarp_release_en.txt; do
  python - "$D/$f" "$P/doc/$f" <<'EOF'
import sys
t = open(sys.argv[1], encoding="utf-8").read()
plain = str.maketrans("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ„”–", 'acelnoszzACELNOSZZ""-')
t = t.translate(plain)
assert all(ord(c) < 128 for c in t), sys.argv[1]
open(sys.argv[2], "w", encoding="ascii", newline="\n").write(t)
EOF
done
cp upstream/devilutionX/LICENSE.md "$P/doc/LICENSE.md"
python tools/make_doc_icon.py "$P/doc.info" --frame tools/icon/drawer.png --crop 0 0 46 40 --transparent --drawer \
  --window 430 200 --pos 140 8 >/dev/null 2>&1
for t in DiabloWarp.txt:doc_pl:12:10 DiabloWarp_en.txt:doc_en:212:10 DiabloWarp_release.txt:doc_pl:12:80 \
         DiabloWarp_release_en.txt:doc_en:212:80 LICENSE.md:doc_i:112:150; do
  IFS=: read -r f pic x y <<EOT
$t
EOT
  python tools/make_doc_icon.py "$P/doc/$f.info" --frame "tools/icon/$pic.png" --crop 0 0 36 44 --transparent \
    --project SYS:Utilities/MultiView --pos "$x" "$y" >/dev/null 2>&1
done
python - "$D/data_ReadMe.txt" "$P/data/ReadMe.txt" <<'EOF'
import sys
t = open(sys.argv[1], encoding="utf-8").read()
t = t.translate(str.maketrans("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ", "acelnoszzACELNOSZZ"))
open(sys.argv[2], "w", encoding="ascii", newline="\n").write(t)
EOF
printf 'Saves and diablo.ini of the game are kept here.\n\nTu gra trzyma zapisy i diablo.ini.\n' > "$P/save/ReadMe.txt"

# never game data or a voice pack in the public release
if find "$P" -iname '*.mpq' -o -iname '*.sv' | grep -q .; then
  echo "refusing: game data in $P" >&2; exit 1
fi

# only the current ZIP in releases/, the older ones to private/old (kept)
mkdir -p private/old
for f in "$R"/DiabloWarp-*.zip; do
  [ -e "$f" ] && [ "$f" != "$R/DiabloWarp-$V.zip" ] && { git rm -q --cached "$f" 2>/dev/null || true; mv "$f" private/old/; }
done
python tools/make_zip.py "$R/DiabloWarp-$V.zip" "$R" DiabloWarp DiabloWarp.info
ls -l "$R/DiabloWarp-$V.zip"

# the full release, only on the PC (private/ is outside git): the same + the game data
# (diabdat.mpq, CD) and the Polish speech converted for the port (pl.mpq) in data/
MPQ=AmigaPortOrginal/diabdat.mpq
PL=private/voice/pl-wav.mpq
if [ -f "$MPQ" ] && [ -f "$PL" ]; then
  for f in private/DiabloWarp-*-full.zip; do
    [ -e "$f" ] && [ "$f" != "private/DiabloWarp-$V-full.zip" ] && mv "$f" private/old/
  done
  python tools/make_zip.py "private/DiabloWarp-$V-full.zip" "$R" DiabloWarp DiabloWarp.info \
    --file "$MPQ" DiabloWarp/data/diabdat.mpq --file "$PL" DiabloWarp/data/pl.mpq
  ls -l "private/DiabloWarp-$V-full.zip"
else
  echo "no $MPQ or $PL: no full release" >&2
fi
