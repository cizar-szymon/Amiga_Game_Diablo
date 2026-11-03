"""Shows a VERIFY failure of the incremental renderer (RAM:vfailN.raw):
   usage: python tools/vdiff.py vfail1.raw out.png
   out.png: incremental | full | difference (red) with the dirty rectangles (green)."""
import struct
import sys
from PIL import Image, ImageDraw

d = open(sys.argv[1], 'rb').read()
w, h, n = struct.unpack('>HHH', d[:6])
inc = d[6:6 + w * h]
full = d[6 + w * h:6 + 2 * w * h]
rects = [struct.unpack('>hhhh', d[6 + 2 * w * h + i * 8:6 + 2 * w * h + i * 8 + 8]) for i in range(n)]
img = Image.new('RGB', (w * 3, h))
a = Image.frombytes('L', (w, h), inc).convert('RGB')
b = Image.frombytes('L', (w, h), full).convert('RGB')
img.paste(a, (0, 0))
img.paste(b, (w, 0))
diff = Image.new('RGB', (w, h))
px = diff.load()
bad = 0
box = [w, h, 0, 0]
for y in range(h):
    for x in range(w):
        if inc[y * w + x] != full[y * w + x]:
            px[x, y] = (255, 0, 0)
            bad += 1
            box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
        else:
            v = full[y * w + x] // 3
            px[x, y] = (v, v, v)
dr = ImageDraw.Draw(diff)
for x, y, rw, rh in rects:
    dr.rectangle([x, y, x + rw - 1, y + rh - 1], outline=(0, 255, 0))
img.paste(diff, (2 * w, 0))
img.save(sys.argv[2])
print(f'{w}x{h}, {bad} pixels differ in {box}, rects {rects}')
