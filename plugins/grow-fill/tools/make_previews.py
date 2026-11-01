#!/usr/bin/env python3
"""growfill_cli をビルドしてサンプル画像から previews/ を作る。
usage: python3 make_previews.py <work_dir>
"""
import os, subprocess, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "previews")
work = sys.argv[1]
os.makedirs(work, exist_ok=True)
exe = os.path.join(work, "growfill_cli")
subprocess.check_call(["g++", "-O2", "-std=c++17", "-Wall", "-Wextra", os.path.join(HERE, "growfill_cli.cpp"), "-o", exe])
conv = os.path.join(HERE, "rgba_convert.py")


def figure(alpha=False):
    W, H = 480, 400
    bg = (0, 0, 0, 0) if alpha else (255, 255, 255, 255)
    im = Image.new("RGBA", (W, H), bg)
    d = ImageDraw.Draw(im)
    k = (20, 20, 20, 255)
    d.ellipse((150, 40, 330, 210), outline=k, width=4)
    d.ellipse((195, 105, 215, 125), outline=k, width=3)
    d.ellipse((265, 105, 285, 125), outline=k, width=3)
    d.arc((210, 130, 270, 175), 20, 160, fill=k, width=4)
    d.rounded_rectangle((170, 210, 310, 370), radius=30, outline=k, width=4)
    d.line((170, 260, 90, 200), fill=k, width=4); d.line((90, 200, 80, 215), fill=k, width=4); d.line((80, 215, 170, 290), fill=k, width=4)
    d.line((310, 260, 400, 330), fill=k, width=4); d.line((400, 330, 390, 345), fill=k, width=4); d.line((390, 345, 312, 290), fill=k, width=4)
    d.line((240, 215, 240, 365), fill=k, width=3)
    d.rectangle((300, 320, 312, 323), fill=bg)  # 3px の切れ目
    return im


def run(src, name, **kw):
    raw_in = os.path.join(work, "in.iprf")
    raw_out = os.path.join(work, "out.iprf")
    png = os.path.join(work, name + ".png")
    src.save(os.path.join(work, "in.png"))
    subprocess.check_call([sys.executable, conv, "to-raw", os.path.join(work, "in.png"), raw_in])
    subprocess.check_call([exe, raw_in, raw_out] + [f"{k}={v}" for k, v in kw.items()])
    subprocess.check_call([sys.executable, conv, "to-png", raw_out, png])
    return Image.open(png)


def strip(ims, bg="white"):
    W = sum(i.width for i in ims)
    c = Image.new("RGBA", (W, ims[0].height), bg)
    x = 0
    for i in ims:
        c.alpha_composite(i, (x, 0)) if i.mode == "RGBA" else c.paste(i, (x, 0))
        x += i.width
    return c


src = figure()
base = dict(ox=200, oy=300, gaps=2, wobble=6, stagger=0.3)
strip([run(src, f"p{p}", progress=p, **base) for p in (0.25, 0.5, 0.75, 1)]).save(os.path.join(OUT, "01_progress.png"))
strip([src, run(src, "nogaps", progress=1, ox=200, oy=300, gaps=0), run(src, "target", progress=1, ox=200, oy=300, target=1)]).save(os.path.join(OUT, "02_gaps_target.png"))
strip([run(src, "w0", progress=0.5, ox=240, oy=120, gaps=2), run(src, "w1", progress=0.5, ox=240, oy=120, gaps=2, wobble=14, wsize=30)]).save(os.path.join(OUT, "03_wobble.png"))
asrc = figure(alpha=True)
strip([run(asrc, "alpha", detect=0, progress=1, ox=200, oy=300, gaps=2, color="0.2,0.55,1")], bg=(255, 255, 255, 255)).save(os.path.join(OUT, "04_alpha_layer.png"))
frames = []
for i in range(24):
    t = i / 23
    f = run(src, f"a{i}", progress=t, **base).convert("RGB")
    frames.append(f)
frames[0].save(os.path.join(OUT, "05_anim.gif"), save_all=True, append_images=frames[1:] + [frames[-1]] * 8, duration=60, loop=0)
