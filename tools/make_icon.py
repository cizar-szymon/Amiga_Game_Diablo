#!/usr/bin/env python3
"""Build the Workbench icon of the DevilutionX Warp port from a picture of the game
(copied from the OpenDUNE port; --stack sets the stack the program gets).

  python tools/make_icon.py OUT.info --frame build/shots/frame_001600.bmp --crop X Y W H
                            [--preview OUT.png] [--tooltypes "SCALE=1" "(STATS)" ...] [--lit-frame] [--drawer]

The frame comes from the PC version (vastar -headless -shots DIR N plus a -script that
starts a game). The icon has a classic 4 colour image (any Workbench) and an OS 3.5+
ColorIcon (FORM ICON with FACE and IMAG chunks, RLE compressed) with the game's colours,
which AmigaOS 3.2 shows. Based on the Wizard of Wor port's icon tool.
"""

import argparse
import struct

from PIL import Image


def rle_pack(values, depth):
    """ColorIcon RLE: ILBM byterun1 on a bit stream of `depth` bit values"""
    out_bits = []

    def emit(v, n):
        for i in range(n - 1, -1, -1):
            out_bits.append((v >> i) & 1)

    i = 0
    while i < len(values):
        run = 1
        while i + run < len(values) and values[i + run] == values[i] and run < 128:
            run += 1
        if run >= 3:
            emit(257 - run, 8)
            emit(values[i], depth)
            i += run
            continue
        lit = 0
        while i + lit < len(values) and lit < 128:
            r = 1
            while i + lit + r < len(values) and values[i + lit + r] == values[i + lit] and r < 3:
                r += 1
            if r >= 3:
                break
            lit += 1
        emit(lit - 1, 8)
        for v in values[i:i + lit]:
            emit(v, depth)
        i += lit
    while len(out_bits) % 8:
        out_bits.append(0)
    return bytes(int("".join(map(str, out_bits[k:k + 8])), 2) for k in range(0, len(out_bits), 8))


def chunk(name, data):
    pad = b"\0" if len(data) & 1 else b""
    return name + struct.pack(">I", len(data)) + data + pad


def color_icon(images, w, h, colors, transparent=None):
    """FORM ICON: FACE and one IMAG per image (normal, selected), one palette;
    transparent: the colour index not drawn (the window shows through)"""
    depth = max(1, (len(colors) - 1).bit_length())
    palette = b"".join(struct.pack(">BBB", *c) for c in colors)
    face = struct.pack(">BBBBH", w - 1, h - 1, 0, 0x11, len(palette) - 1)
    body = b"ICON" + chunk(b"FACE", face)
    for idx in images:
        pixels = rle_pack(idx, depth)
        imag = struct.pack(">BBBBBBHH", transparent or 0, len(colors) - 1, 0x03 if transparent is not None else 0x02,
                           1, 0, depth, len(pixels) - 1,
                           len(palette) - 1) + pixels + palette
        body += chunk(b"IMAG", imag)
    return b"FORM" + struct.pack(">I", len(body)) + body


# the default Workbench 3.x colours: grey, black, white, blue
WB4 = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]


# the colour of a picture's transparent pixels (--transparent)
CLEAR = (255, 0, 255)


def nearest_wb(c):
    if c == CLEAR:
        return 0
    return min(range(4), key=lambda i: sum((c[k] - WB4[i][k]) ** 2 for k in range(3)))


def planar(rgb, w, h):
    """the classic 2 bitplane image in the 4 Workbench colours"""
    words = (w + 15) // 16
    planes = []
    for p in range(2):
        data = bytearray()
        for y in range(h):
            bits = 0
            for x in range(words * 16):
                v = nearest_wb(rgb[y * w + x]) if x < w else 0
                bits = (bits << 1) | ((v >> p) & 1)
            data += bits.to_bytes(words * 2, "big")
        planes.append(bytes(data))
    return b"".join(planes)


def tool_types(types):
    out = struct.pack(">I", (len(types) + 1) * 4)
    for t in types:
        b = t.encode("latin-1") + b"\0"
        out += struct.pack(">I", len(b)) + b
    return out


def drawer_data():
    """OldDrawerData (the drawer's window: NewWindow + current x, y), placed after the DiskObject"""
    return struct.pack(">hhhhBBIIIIIIIhhHHHii", 50, 50, 400, 200, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0,
                       90, 40, 0xFFFF, 0xFFFF, 1, 0, 0)


def disk_object(rgb, w, h, types, selected=None, drawer=False, project=None, pos=None):
    """selected: the image shown when the icon is selected (else the colours inverted);
    drawer: a drawer's icon (WBDRAWER, the window's data) instead of a tool's"""
    flags = 0x0006 if selected else 0x0004	# GADGIMAGE | GADGHIMAGE (two images)
    gadget = struct.pack(">IhhhhHHHIIIIIHI", 0, 0, 0, w, h + 1, flags, 0x0003, 0x0001,
                         1, 1 if selected else 0, 0, 0, 0, 0, 1)
    px, py = pos if pos else (-0x80000000, -0x80000000)	# the place in the drawer's window (or none)
    if drawer:
        head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
            ">BBIIiiIII", 2, 0, 0, 0, px, py, 1, 0, 0) + drawer_data()
        types = []
    elif project:
        # a project (a text): double-click opens it with its default tool
        head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
            ">BBIIiiIII", 4, 0, 1, 1 if types else 0, px, py, 0, 0, STACK)
    else:
        head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
            ">BBIIiiIII", 3, 0, 0, 1 if types else 0, px, py, 0, 0, STACK)
    image = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0) + planar(rgb, w, h)
    if selected:
        image += struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0) + planar(selected, w, h)
    colors = sorted(set(rgb) | set(selected or []))
    if len(colors) > 256:
        raise SystemExit("more than 256 colours")
    remap = {c: i for i, c in enumerate(colors)}
    images = [[remap[c] for c in rgb]] + ([[remap[c] for c in selected]] if selected else [])
    # a drawer of an OS 2+ icon (gadget UserData 1): DrawerData2 (flags: show icons, view by icon)
    extra = struct.pack(">IH", 1, 1) if drawer else b""
    transparent = remap.get(CLEAR)
    tool = b""
    if project:
        t = project.encode("latin-1") + b"\0"
        tool = struct.pack(">I", len(t)) + t
    return head + image + tool + (tool_types(types) if types else b"") + extra + color_icon(images, w, h, colors, transparent)


def lit_frame(rgb, w, h, width=3):
    """the selected image: the picture with a lit (gold) frame round it"""
    out = list(rgb)
    shades = [(255, 230, 120), (255, 200, 60), (200, 140, 20)]	# outside -> inside
    for y in range(h):
        for x in range(w):
            d = min(x, y, w - 1 - x, h - 1 - y)
            if d < width:
                out[y * w + x] = shades[min(d, len(shades) - 1)]
    return out


def dark(rgb, factor=0.65):
    """the selected image: the picture darker (its transparent pixels kept)"""
    return [c if c == CLEAR else tuple(int(v * factor) for v in c) for c in rgb]


STACK = 16384


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--frame", required=True, help="BMP/PNG frame of the game")
    ap.add_argument("--crop", nargs=4, type=int, required=True, metavar=("X", "Y", "W", "H"))
    ap.add_argument("--preview")
    ap.add_argument("--tooltypes", nargs="*", default=[])
    ap.add_argument("--lit-frame", action="store_true", help="selected image: a lit frame round the picture")
    ap.add_argument("--drawer", action="store_true", help="a drawer's icon (the game's directory) instead of the program's")
    ap.add_argument("--project", metavar="TOOL", help="a project's icon (a text) opened by TOOL, e.g. SYS:Utilities/MultiView")
    ap.add_argument("--pos", nargs=2, type=int, metavar=("X", "Y"), help="the icon's place in its drawer's window")
    ap.add_argument("--stack", type=int, default=16384, help="stack size for the program (bytes)")
    ap.add_argument("--transparent", action="store_true", help="magenta (255,0,255) pixels transparent, the selected image darker")
    args = ap.parse_args()
    global STACK
    STACK = args.stack
    x, y, w, h = args.crop
    img = Image.open(args.frame).convert("RGB").crop((x, y, x + w, y + h))
    rgb = list(img.getdata())
    selected = lit_frame(rgb, w, h) if args.lit_frame else dark(rgb) if args.transparent else None
    open(args.out, "wb").write(disk_object(rgb, w, h, args.tooltypes, selected, args.drawer, args.project, args.pos))
    if args.preview:
        img.resize((w * 6, h * 6), Image.NEAREST).save(args.preview)
        if selected:
            sel = Image.new("RGB", (w, h))
            sel.putdata(selected)
            sel.resize((w * 6, h * 6), Image.NEAREST).save(args.preview.replace(".png", "-selected.png"))
    print(f"{args.out}: {w}x{h}, {len(set(rgb))} colours, tool types {args.tooltypes}")


if __name__ == "__main__":
    main()
