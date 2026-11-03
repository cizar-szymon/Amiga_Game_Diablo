#!/usr/bin/env python3
"""A release ZIP for the Amiga (tools/make_release.sh).

  python tools/make_zip.py OUT.zip BASE NAME... [--add DIR ARCDIR]...

NAME: files or directories under BASE, stored with their path from BASE.
--add DIR ARCDIR: the files of DIR (not recursive; data: only *.PAK, music: *.RAW)
stored under ARCDIR - the full release's game data and music.
Directories get their own entries (empty data/ unpacks too); the files are
deflated, the music stored (8-bit samples: deflate saves little, costs time).
"""
import argparse
import os
import zipfile

KEEP = {"data": (".PAK",), "music": (".RAW",)}
EXECUTABLES = {"OpenDUNE", "MakeMusic"}	# the programs: must get the Amiga's E bit


def put(z, path, arcname, method=zipfile.ZIP_DEFLATED):
    """A file with Unix permissions (made-by Unix): Amiga UnZip turns them into the
    protection bits - without them the program unpacks without E (cannot run)."""
    info = zipfile.ZipInfo.from_file(path, arcname)
    info.create_system = 3
    mode = 0o755 if os.path.basename(arcname) in EXECUTABLES else 0o644
    info.external_attr = (0o100000 | mode) << 16
    info.compress_type = method
    with open(path, "rb") as f:
        z.writestr(info, f.read())


def put_dir(z, arcname):
    info = zipfile.ZipInfo(arcname.rstrip("/") + "/")
    info.create_system = 3
    info.external_attr = (0o040755 << 16) | 0x10
    z.writestr(info, "")


def add_tree(z, base, name):
    path = os.path.join(base, name)
    if os.path.isfile(path):
        put(z, path, name.replace(os.sep, "/"))
        return
    for root, dirs, files in os.walk(path):
        dirs.sort()
        rel = os.path.relpath(root, base).replace(os.sep, "/")
        put_dir(z, rel)
        for f in sorted(files):
            put(z, os.path.join(root, f), rel + "/" + f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("base")
    ap.add_argument("names", nargs="+")
    ap.add_argument("--add", nargs=2, action="append", default=[], metavar=("DIR", "ARCDIR"))
    a = ap.parse_args()

    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n in a.names:
            add_tree(z, a.base, n)
        for d, arcdir in a.add:
            ext = KEEP.get(os.path.basename(d.rstrip("/\\")).lower(), ("",))
            for f in sorted(os.listdir(d)):
                if not f.upper().endswith(ext):
                    continue
                method = zipfile.ZIP_STORED if f.upper().endswith(".RAW") else zipfile.ZIP_DEFLATED
                put(z, os.path.join(d, f), arcdir + "/" + f, method)
    print(a.out, os.path.getsize(a.out), "bytes")


if __name__ == "__main__":
    main()
