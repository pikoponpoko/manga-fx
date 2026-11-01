#!/usr/bin/env python3
"""goomerge_cli をビルドしてサンプル画像から previews/ を作る。
usage: python3 make_previews.py <work_dir>
"""
import math, os, subprocess, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "previews")
work = sys.argv[1]
os.makedirs(work, exist_ok=True)
exe = os.path.join(work, "goomerge_cli")
subprocess.check_call(["g++", "-O2", "-std=c++17", "-pthread", "-Wall", "-Wextra", os.path.join(HERE, "goomerge_cli.cpp"), "-o", exe])
conv = os.path.join(HERE, "rgba_convert.py")
W, H = 400, 300


def scene(t=0.0):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # 離れていく 2 つの円、角のある四角と三角、すき間の空いた文字のような棒
    s = 40 + 50 * t
    d.ellipse((140 - s - 45, 70, 140 - s + 45, 160), fill=(255, 90, 77, 255))
    d.ellipse((140 + s - 40, 75, 140 + s + 40, 155), fill=(80, 150, 255, 255))
    d.rectangle((40, 190, 120, 270), fill=(255, 200, 60, 255))
    d.polygon([(130, 270), (170, 190), (210, 270)], fill=(60, 200, 120, 255))
    for i in range(4):
        d.rectangle((250 + i * 30, 180, 270 + i * 30, 270), fill=(140, 90, 220, 255))
    d.ellipse((300, 40, 350, 90), fill=(255, 140, 200, 255))
    d.ellipse((345, 80, 380, 115), fill=(255, 140, 200, 255))
    return im


def run(src, name, bg=(255, 255, 255, 255), **kw):
    raw_in, raw_out = os.path.join(work, "in.iprf"), os.path.join(work, "out.iprf")
    src.save(os.path.join(work, "in.png"))
    subprocess.check_call([sys.executable, conv, "to-raw", os.path.join(work, "in.png"), raw_in])
    subprocess.check_call([exe, raw_in, raw_out] + [f"{k}={v}" for k, v in kw.items()])
    png = os.path.join(work, name + ".png")
    subprocess.check_call([sys.executable, conv, "to-png", raw_out, png])
    out = Image.open(png)
    m = (out.width - W) // 2
    c = Image.new("RGBA", (W, H), bg)
    c.alpha_composite(out.crop((m, m, m + W, m + H)))
    return c


def strip(ims):
    c = Image.new("RGBA", (sum(i.width for i in ims), H), "white")
    x = 0
    for i in ims:
        c.paste(i, (x, 0)); x += i.width
    return c


src = scene(0.3)
plain = Image.new("RGBA", (W, H), "white"); plain.alpha_composite(src)
strip([plain, run(src, "solid", radius=16), run(src, "solid_fill", radius=20, round=10, color=1, fill="0.1,0.1,0.1")]).save(os.path.join(OUT, "01_before_solid.png"))
dark = (30, 40, 70, 255)
strip([run(src, "bubble", bg=dark, radius=16, style=1),
       run(src, "bubble_fill", bg=dark, radius=20, round=6, style=1, color=1, fill="0.6,0.85,1", bopacity=0.15, rim=8)]).save(os.path.join(OUT, "02_bubble.png"))
frames = []
for i in range(30):
    t = 0.5 - 0.5 * math.cos(2 * math.pi * i / 30)
    frames.append(run(scene(t), f"a{i}", radius=18).convert("RGB"))
frames[0].save(os.path.join(OUT, "03_split_anim.gif"), save_all=True, append_images=frames[1:], duration=60, loop=0)
