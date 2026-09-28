"""Render the character harness's frame dump to images (host preview).

    HARNESS_DUMP=frames.txt tests/character_harness/harness.exe <scenario> <seed>
    python tools/char_preview.py frames.txt out_dir [--every N] [--from MS] [--to MS] [--gif] [--sheet]

Draws what the renderer drew - centred parts as rounded boxes, custom parts
(lids) as their rects / triangles / lines clipped to their own box - at 4x
and downsamples, so edges are anti-aliased roughly as on the device. The
round screen is the frame. An approximation for reviewing motion and poses,
not a pixel-exact copy of LVGL.
"""
import argparse
import os
from PIL import Image, ImageDraw

S = 4                      # supersampling
W = 240


def col(hexs):
    v = int(hexs, 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def parse(path):
    frames = []
    cur = None
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "F":
            cur = {"t": int(p[1]), "ops": []}
            frames.append(cur)
        elif cur is not None:
            cur["ops"].append(p)
    return frames


def render(frame):
    img = Image.new("RGB", (W * S, W * S), (8, 8, 8))
    for op in frame["ops"]:
        kind = op[0]
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        clip = None
        if kind == "R":                      # a centred part: x1 y1 x2 y2 radius bg opa border_w border_c
            x1, y1, x2, y2, rad = (int(v) for v in op[1:6])
            bg, opa, bw, bc = col(op[6]), int(op[7]), int(op[8]), col(op[9])
            r = min(rad, (x2 - x1 + 1) // 2, (y2 - y1 + 1) // 2)
            box = [x1 * S, y1 * S, (x2 + 1) * S - 1, (y2 + 1) * S - 1]
            if opa > 0:
                d.rounded_rectangle(box, radius=r * S, fill=bg + (opa,))
            if bw > 0:
                d.rounded_rectangle(box, radius=r * S, outline=bc + (255,), width=bw * S)
        elif kind == "r":                    # custom rect: x1 y1 x2 y2 radius color, clip box
            x1, y1, x2, y2, rad = (int(v) for v in op[1:6])
            c = col(op[6])
            clip = [int(v) for v in op[7:11]]
            r = min(max(rad, 0), (x2 - x1 + 1) // 2, (y2 - y1 + 1) // 2)
            d.rounded_rectangle([x1 * S, y1 * S, (x2 + 1) * S - 1, (y2 + 1) * S - 1], radius=r * S, fill=c + (255,))
        elif kind == "t":                    # triangle: 3 points, color, clip box
            pts = [(float(op[1 + 2 * k]) * S, float(op[2 + 2 * k]) * S) for k in range(3)]
            d.polygon(pts, fill=col(op[7]) + (255,))
            clip = [int(v) for v in op[8:12]]
        elif kind == "l":                    # line: p1 p2 width color
            x1, y1, x2, y2 = (float(v) * S for v in op[1:5])
            d.line([(x1, y1), (x2, y2)], fill=col(op[6]) + (255,), width=max(1, int(op[5]) * S))
        if clip:
            mask = Image.new("L", img.size, 0)
            ImageDraw.Draw(mask).rectangle([clip[0] * S, clip[1] * S, (clip[2] + 1) * S - 1, (clip[3] + 1) * S - 1], fill=255)
            alpha = Image.composite(layer.getchannel("A"), Image.new("L", img.size, 0), mask)
            layer.putalpha(alpha)
        img.paste(layer, (0, 0), layer)
    img = img.resize((W, W), Image.LANCZOS)
    ring = Image.new("L", (W, W), 0)
    ImageDraw.Draw(ring).ellipse([0, 0, W - 1, W - 1], fill=255)
    out = Image.new("RGB", (W, W), (40, 40, 40))
    out.paste(img, (0, 0), ring)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("out")
    ap.add_argument("--every", type=int, default=1)
    ap.add_argument("--from", dest="t0", type=int, default=0)
    ap.add_argument("--to", dest="t1", type=int, default=10 ** 9)
    ap.add_argument("--gif", action="store_true")
    ap.add_argument("--sheet", action="store_true", help="one contact sheet of the selected frames")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    frames = [f for f in parse(a.dump) if a.t0 <= f["t"] <= a.t1][:: a.every]
    imgs = []
    for f in frames:
        im = render(f)
        imgs.append((f["t"], im))
        if not a.gif and not a.sheet:
            im.save(os.path.join(a.out, "f%07d.png" % f["t"]))
    if a.gif and imgs:
        imgs[0][1].save(os.path.join(a.out, "anim.gif"), save_all=True, append_images=[i for _, i in imgs[1:]],
                        duration=33 * a.every, loop=0)
    if a.sheet and imgs:
        cols = min(8, len(imgs))
        rows = (len(imgs) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * W, rows * (W + 14)), (20, 20, 20))
        d = ImageDraw.Draw(sheet)
        for k, (t, im) in enumerate(imgs):
            x, y = (k % cols) * W, (k // cols) * (W + 14)
            sheet.paste(im, (x, y))
            d.text((x + 4, y + W), "%d ms" % t, fill=(200, 200, 200))
        sheet.save(os.path.join(a.out, "sheet.png"))
    print("%d frames" % len(imgs))


if __name__ == "__main__":
    main()
