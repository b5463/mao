#!/usr/bin/env python3
"""The marks of MAO's light field, cut from the ODD JOBS symbol.

    python tools/odd_field_marks.py [--width 14]

Reads assets/sprites/odd_jobs_symbol.png (written by tools/odd_jobs_mark.py
from the official artwork) and writes components/mao_ui/mao_field_marks.c:
ten A8 masks, all the same size, centred, crisp pixel art (every pixel
on or off: the dot-matrix look of MAO's other dots) -

  MARK   the whole symbol                       BLOCK  its right part (right of the slit)
  TAIL   its left part (rising into the slit)    SLIT   a thin upright the symbol's height
  DASH   a short flat stroke (mid-blink)         DOT    a dot
  HEX    the symbol's outer form, a hexagon      RING   the same, hollow
  SLIT2  two slits                               DOTS3  three dots along the tail's slant

Coverage only; MAO draws them in its ink.
"""
import argparse
import math
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORDER = ["MARK", "BLOCK", "TAIL", "SLIT", "DASH", "DOT", "HEX", "RING", "SLIT2", "DOTS3"]


def build(alpha, slit, w, h, crisp, gamma):
    W, H = alpha.size

    def solid(img):
        if crisp:
            return img.point(lambda v: 255 if v >= crisp else 0)
        return img.point(lambda v: int(255 * (v / 255.0) ** gamma))

    def shrink(img):
        return solid(img.resize((w, h), Image.BOX))

    tail = alpha.copy()
    tail.paste(0, (slit, 0, W, H))
    block = alpha.copy()
    block.paste(0, (0, 0, slit, H))
    masks = {"MARK": shrink(alpha), "BLOCK": shrink(block), "TAIL": shrink(tail)}
    if crisp:
        # the slit, a pixel wide, cut where it falls - in pixel art it must be deliberate
        cs = int(slit * w / W)
        for name in ("MARK", "BLOCK"):
            for y in range(h):
                masks[name].putpixel((cs, y), 0)

    def hexagon(ring):
        im = Image.new("L", (w * 8, h * 8), 0)
        dd = ImageDraw.Draw(im)
        cx, cy, r = w * 4, h * 4, h * 4 - 2
        pts = [(cx + r * math.cos(math.pi / 2 + k * math.pi / 3) * 1.05, cy + r * math.sin(math.pi / 2 + k * math.pi / 3))
               for k in range(6)]
        dd.polygon(pts, fill=255)
        if ring:
            ri = r - 20
            dd.polygon([(cx + ri * math.cos(math.pi / 2 + k * math.pi / 3) * 1.05, cy + ri * math.sin(math.pi / 2 + k * math.pi / 3))
                        for k in range(6)], fill=0)
        return solid(im.resize((w, h), Image.BOX))

    masks["HEX"] = hexagon(False)
    masks["RING"] = hexagon(True)
    big = 8                                  # the drawn marks at 8x, reduced
    for name, draw in (
        ("SLIT", lambda d: d.rectangle([w * big // 2 - 12, big, w * big // 2 + 11, h * big - big - 1], fill=255)),
        ("DASH", lambda d: d.rectangle([2 * big, h * big // 2 - big, w * big - 2 * big - 1, h * big // 2 + big - 1], fill=255)),
        ("DOT", lambda d: d.ellipse([w * big // 2 - 17, h * big // 2 - 17, w * big // 2 + 17, h * big // 2 + 17], fill=255)),
        ("SLIT2", lambda d: (d.rectangle([w * big // 2 - 26, big, w * big // 2 - 7, h * big - big - 1], fill=255),
                             d.rectangle([w * big // 2 + 6, big, w * big // 2 + 25, h * big - big - 1], fill=255))),
        ("DOTS3", lambda d: [d.ellipse([w * big // 2 + k * 30 - 13, h * big // 2 - k * 22 - 13,
                                        w * big // 2 + k * 30 + 13, h * big // 2 - k * 22 + 13], fill=255) for k in (-1, 0, 1)]),
    ):
        im = Image.new("L", (w * big, h * big), 0)
        draw(ImageDraw.Draw(im))
        masks[name] = solid(im.resize((w, h), Image.BOX))
    return masks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=14, help="mark width in px")
    ap.add_argument("--bold", type=int, default=21, help="dilation of the symbol, source px (odd; ~0.6 display px)")
    ap.add_argument("--gamma", type=float, default=0.7, help="(--crisp 0 only) coverage curve (< 1 = solider edges)")
    ap.add_argument("--crisp", type=int, default=110, help="the coverage at which a pixel is on (0 = anti-aliased)")
    a = ap.parse_args()
    src = os.path.join(ROOT, "assets", "sprites", "odd_jobs_symbol.png")
    alpha = Image.open(src).getchannel("A")
    W, H = alpha.size
    # the slit: the emptiest column in the middle third
    cols = [sum(alpha.getpixel((x, y)) for y in range(0, H, 4)) for x in range(W)]
    slit = min(range(W // 3, 2 * W // 3), key=lambda x: cols[x])
    # heavier: the symbol grown a little at full size, so its strokes survive
    # the reduction; the slit re-cut about a display px wide, always open
    if a.bold > 1:
        alpha = alpha.filter(ImageFilter.MaxFilter(a.bold | 1))
        gap = a.bold + 24
        alpha.paste(0, (slit - gap // 2, 0, slit + gap // 2, H))
    w = a.width
    h = max(3, round(w * H / W))
    sets = [("PX", build(alpha, slit, w, h, a.crisp, a.gamma))]
    out = [
        "/*",
        " * The marks of MAO's light field, cut from the ODD JOBS symbol (A8 coverage,",
        " * %dx%d each, crisp pixel art). Generated by" % (w, h),
        " * tools/odd_field_marks.py from assets/sprites/odd_jobs_symbol.png. Do not",
        " * edit by hand.",
        " */",
        "#include \"mao_ui_priv.h\"",
        "",
    ]
    for sn, masks in sets:
        for name in ORDER:
            data = list(masks[name].getdata())
            out.append("static const uint8_t kFm_%s_%s[%d * %d] = {" % (sn, name, w, h))
            for r in range(h):
                out.append("    " + ", ".join("0x%02x" % v for v in data[r * w:(r + 1) * w]) + ",")
            out.append("};")
            out.append("")
    out.append("const lv_image_dsc_t mao_field_mark[MAO_FIELD_MARKS] = {")
    for name in ORDER:
        out.append("    [MAO_FM_%s] = { .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, "
                   ".w = %d, .h = %d, .stride = %d }, .data_size = sizeof(kFm_PX_%s), .data = kFm_PX_%s },"
                   % (name, w, h, w, name, name))
    out.append("};")
    out.append("")
    path = os.path.join(ROOT, "components", "mao_ui", "mao_field_marks.c")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out))
    print("slit at %d of %d; marks %dx%d -> %s" % (slit, W, w, h, os.path.relpath(path, ROOT)))


if __name__ == "__main__":
    main()
