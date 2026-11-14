"""The Hellfire icon picture: the HELLFIRE logo of the game's main menu
(a 640x480 screenshot, tools/shot.sh) scaled into 120x40 on black.
   python tools/icon/make_hellfire_logo.py menu.png tools/icon/hellfire_logo.png"""
import sys
from PIL import Image

src = Image.open(sys.argv[1]).convert("RGB")
logo = src.crop((66, 78, 576, 142))		# the logo with its flames
w = 120
h = round(logo.height * w / logo.width)
logo = logo.resize((w, h), Image.LANCZOS)
out = Image.new("RGB", (w, 40), (0, 0, 0))
out.paste(logo, (0, (40 - h) // 2))
# an icon has at most 256 colours, with the lit frame (make_icon.py --lit-frame)
out = out.quantize(240).convert("RGB")
out.save(sys.argv[2])
print(sys.argv[2], out.size)
