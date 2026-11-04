"""Crops the FPS counter (top left) of several screenshots into one image,
   to read the old port's on-screen frame rate (-f).
   usage: python tools/fpscrop.py out.png shot1.png shot2.png ..."""
import sys
from PIL import Image

shots = sys.argv[2:]
row = Image.new('RGB', (160, 30 * len(shots)))
for k, name in enumerate(shots):
    row.paste(Image.open(name).convert('RGB').crop((0, 48, 160, 78)), (0, 30 * k))
row.resize((480, 90 * len(shots))).save(sys.argv[1])
