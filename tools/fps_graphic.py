"""Picture of the old-vs-new port comparison (docs/porownanie_fps.png).
   usage: python tools/fps_graphic.py [out.png]"""
import sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "docs/porownanie_fps.png"
ROWS = [  # scene, old (min, max), new (min, max)
    ("Tristram, postój", (17, 17), (91, 91)),
    ("Tristram, marsz", (13, 17), (47, 66)),
    ("Loch, marsz", (10, 13), (17, 26)),
    ("Loch, walka ze szkieletami", (11, 13), (46, 46)),
]
W, H = 1600, 900
BG, FG, MUTED, LINE = (24, 22, 28), (236, 232, 226), (150, 146, 140), (70, 66, 74)
OLD, NEW, GOOD = (120, 116, 122), (214, 92, 52), (120, 196, 120)


def font(name, size):
    for path in (f"C:/Windows/Fonts/{name}", "C:/Windows/Fonts/segoeui.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


title = font("seguisb.ttf", 46)
sub = font("segoeui.ttf", 24)
head = font("seguisb.ttf", 24)
cell = font("segoeui.ttf", 28)
big = font("seguisb.ttf", 30)
small = font("segoeui.ttf", 20)

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)
M = 56
# frame
d.rounded_rectangle([24, 24, W - 24, H - 24], radius=18, outline=FG, width=3)
d.text((M, 52), "Diablo na Amidze z Warpem: nowy port wobec starego", font=title, fill=FG)
d.text((M, 116), "Amiga 2: A4000 + Warp 7060 (MC68060), 100 MHz, AmigaOS 3.2.2.1, ekran 640×480×8", font=sub, fill=MUTED)
d.text((M, 148), "stary port: DevilutionX 1.0.0 (Aminet, 2020)   nowy port: DevilutionX 1.6.0-dev dla Warpa, z dźwiękiem   4.11.2026",
       font=sub, fill=MUTED)

# columns: scene | old | new | speedup | bars
cx = [M, 470, 640, 810, 960]
y = 210
d.text((cx[0], y), "Scena", font=head, fill=MUTED)
d.text((cx[1], y), "stary", font=head, fill=MUTED)
d.text((cx[2], y), "nowy", font=head, fill=MUTED)
d.text((cx[3], y), "szybciej", font=head, fill=MUTED)
d.text((cx[4], y), "kl./s (zakres)", font=head, fill=MUTED)
y += 44
d.line([M, y, W - M, y], fill=LINE, width=2)

bx0, bx1 = cx[4], W - M - 90
scale = lambda v: bx0 + (bx1 - bx0) * v / 100.0
rng = lambda v: str(v[0]) if v[0] == v[1] else f"{v[0]}–{v[1]}"
rowh = 128
for i, (scene, old, new) in enumerate(ROWS):
    top = y + 18 + i * rowh
    mid = top + rowh / 2 - 14
    sp = ((new[0] + new[1]) / 2) / ((old[0] + old[1]) / 2)
    d.text((cx[0], mid - 4), scene, font=cell, fill=FG)
    d.text((cx[1], mid - 4), rng(old), font=cell, fill=FG)
    d.text((cx[2], mid - 4), rng(new), font=big, fill=FG)
    d.text((cx[3], mid - 4), f"{sp:.1f}×".replace(".", ","), font=big, fill=GOOD)
    for k, (v, col, dy) in enumerate(((old, OLD, 26), (new, NEW, 62))):
        yy = top + dy
        x0, x1 = scale(v[0]), max(scale(v[1]), scale(v[0]) + 10)
        d.rounded_rectangle([scale(0), yy, x0, yy + 22], radius=5, fill=tuple(int(c * 0.35 + BG[j] * 0.65) for j, c in enumerate(col)))
        d.rounded_rectangle([x0, yy, x1, yy + 22], radius=5, fill=col)
        d.text((x1 + 10, yy - 2), rng(v), font=small, fill=MUTED)
    d.line([M, top + rowh - 6, W - M, top + rowh - 6], fill=LINE, width=1)

# axis ticks under the bars
ay = y + 18 + len(ROWS) * rowh + 4
for t in range(0, 101, 20):
    x = scale(t)
    d.line([x, y + 10, x, ay - 6], fill=LINE, width=1)
    tw = d.textlength(str(t), font=small)
    d.text((x - tw / 2, ay), str(t), font=small, fill=MUTED)

# legend
ly = H - 70
d.rounded_rectangle([M, ly + 4, M + 22, ly + 26], radius=4, fill=OLD)
d.text((M + 32, ly), "stary port (Aminet, 2020)", font=small, fill=FG)
d.rounded_rectangle([M + 330, ly + 4, M + 352, ly + 26], radius=4, fill=NEW)
d.text((M + 362, ly), "nowy port (Warp)", font=small, fill=FG)
note = "szybciej = środek zakresu nowego / środek zakresu starego"
d.text((W - M - d.textlength(note, font=small), ly), note, font=small, fill=MUTED)

img.save(OUT)
print(OUT)
