#!/usr/bin/env python3
"""flowloop_cli をビルドしてサンプル画像から previews/ を作る。
usage: python3 make_previews.py <work_dir>
"""
import math, os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "previews")
work = sys.argv[1]
os.makedirs(work, exist_ok=True)
exe = os.path.join(work, "flowloop_cli")
subprocess.check_call(["g++", "-O2", "-std=c++17", "-pthread", "-Wall", "-Wextra", os.path.join(HERE, "flowloop_cli.cpp"), "-o", exe])
conv = os.path.join(HERE, "rgba_convert.py")
W, H = 400, 300
rng = np.random.default_rng(3)


def noise(scale, seed):
    r = np.random.default_rng(seed)
    small = r.random((H // scale + 2, W // scale + 2))
    im = Image.fromarray((small * 255).astype(np.uint8)).resize((W + 2 * scale, H + 2 * scale), Image.BICUBIC)
    return np.asarray(im, dtype=np.float32)[:H, :W] / 255.0


def river_scene():
    # 空・山・川（川の中は波の模様）
    y = np.linspace(0, 1, H)[:, None] * np.ones((1, W))
    img = np.zeros((H, W, 3), np.float32)
    sky = np.stack([0.45 + 0.3 * y, 0.7 + 0.2 * y, np.ones_like(y)], -1)
    cl = np.clip((noise(40, 1) * 0.7 + noise(12, 2) * 0.3 - 0.45) * 3, 0, 1)[..., None]
    img[:] = sky * (1 - cl) + cl
    im = Image.fromarray((img * 255).astype(np.uint8))
    d = ImageDraw.Draw(im)
    d.polygon([(0, 170), (90, 90), (170, 160), (260, 70), (400, 170), (400, 300), (0, 300)], fill=(70, 120, 90))
    river = Image.new("L", (W, H), 0)
    ImageDraw.Draw(river).polygon([(170, 165), (230, 165), (330, 300), (60, 300)], fill=255)
    wav = noise(6, 5) * 0.6 + noise(18, 6) * 0.4
    water = np.stack([0.15 + 0.5 * wav, 0.4 + 0.45 * wav, 0.75 + 0.25 * wav], -1)
    wim = Image.fromarray((np.clip(water, 0, 1) * 255).astype(np.uint8))
    im.paste(wim, (0, 0), river)
    return im.convert("RGBA")


def run(src, name, **kw):
    raw_in, raw_out = os.path.join(work, "in.iprf"), os.path.join(work, "out.iprf")
    src.save(os.path.join(work, "in.png"))
    subprocess.check_call([sys.executable, conv, "to-raw", os.path.join(work, "in.png"), raw_in])
    args = []
    for k, v in kw.items():
        if k in ("guides", "areas"):
            args += [k[:-1] + "=" + ";".join(f"{x},{y}" for x, y in g) for g in v]
        else:
            args.append(f"{k}={v}")
    subprocess.check_call([exe, raw_in, raw_out] + args, stderr=subprocess.DEVNULL)
    png = os.path.join(work, name + ".png")
    subprocess.check_call([sys.executable, conv, "to-png", raw_out, png])
    return Image.open(png).convert("RGB")


def gif(frames, path):
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=50, loop=0)


river = river_scene()
guides = [[(200, 160), (195, 220), (190, 300)], [(185, 170), (140, 240), (100, 300)], [(215, 170), (260, 240), (300, 300)],
          [(60, 60), (200, 50), (380, 60)], [(20, 120), (200, 110), (390, 120)]]
# 流れる範囲: 川と空（山は動かさない）
areas = [[(170, 165), (230, 165), (330, 300), (60, 300)],
         [(0, 0), (400, 0), (400, 170), (260, 70), (170, 160), (90, 90), (0, 170)]]
# 静止画 + ガイドの線
g = river.convert("RGB").copy()
dr = ImageDraw.Draw(g)
for pl in areas:
    dr.line(pl + [pl[0]], fill=(255, 255, 0), width=2)
for pl in guides:
    dr.line(pl, fill=(255, 120, 0), width=3)
    (x0, y0), (x1, y1) = pl[-2], pl[-1]
    a = math.atan2(y1 - y0, x1 - x0)
    dr.polygon([(x1, y1), (x1 - 12 * math.cos(a - 0.4), y1 - 12 * math.sin(a - 0.4)), (x1 - 12 * math.cos(a + 0.4), y1 - 12 * math.sin(a + 0.4))], fill=(255, 120, 0))
g.save(os.path.join(OUT, "01_guides.png"))
N = 24
gif([run(river, f"r{i}", source=0, phase=i / N, speed=50, influence=45, feather=6, guides=guides, areas=areas) for i in range(N)], os.path.join(OUT, "02_guides_loop.gif"))

tex = Image.fromarray((np.stack([noise(10, 7), noise(25, 8) * 0.5 + 0.3, noise(5, 9) * 0.3 + 0.6], -1) * 255).astype(np.uint8)).convert("RGBA")
gif([run(tex, f"s{i}", source=4, phase=i / N, speed=60, cx=200, cy=150) for i in range(N)], os.path.join(OUT, "03_swirl_loop.gif"))
