#!/usr/bin/env python3
"""castshadow_cli をビルドしてサンプル画像から previews/ を作る。
usage: python3 make_previews.py <work_dir>
"""
import math, os, subprocess, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "previews")
work = sys.argv[1]
os.makedirs(work, exist_ok=True)
exe = os.path.join(work, "castshadow_cli")
subprocess.check_call(["g++", "-O2", "-std=c++17", "-pthread", "-Wall", "-Wextra", os.path.join(HERE, "castshadow_cli.cpp"), "-o", exe])
conv = os.path.join(HERE, "rgba_convert.py")
W, H = 480, 400


def scene():
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    k = (20, 20, 20, 255)
    d.ellipse((200, 60, 280, 140), fill=(255, 200, 60, 255), outline=k, width=4)
    d.rounded_rectangle((195, 140, 285, 300), radius=20, fill=(80, 150, 255, 255), outline=k, width=4)
    d.rectangle((60, 250, 140, 330), fill=(255, 90, 77, 255), outline=k, width=4)
    d.polygon([(360, 330), (400, 240), (440, 330)], fill=(60, 200, 120, 255), outline=k)
    return im


def run(src, name, **kw):
    raw_in, raw_out = os.path.join(work, "in.iprf"), os.path.join(work, "out.iprf")
    src.save(os.path.join(work, "in.png"))
    subprocess.check_call([sys.executable, conv, "to-raw", os.path.join(work, "in.png"), raw_in])
    subprocess.check_call([exe, raw_in, raw_out] + [f"{k}={v}" for k, v in kw.items()])
    png = os.path.join(work, name + ".png")
    subprocess.check_call([sys.executable, conv, "to-png", raw_out, png])
    out = Image.open(png)
    # 出力は四辺が広がっているので元の大きさで切り出して白地に置く
    m = (out.width - W) // 2
    bg = Image.new("RGBA", (W, H), (255, 255, 255, 255))
    bg.alpha_composite(out.crop((m, m, m + W, m + H)))
    if "lx" in kw and kw.get("light", 0) == 0:
        ImageDraw.Draw(bg).ellipse((kw["lx"] - 6, kw["ly"] - 6, kw["lx"] + 6, kw["ly"] + 6), outline=(255, 160, 0, 255), width=3)
    return bg


def strip(ims):
    c = Image.new("RGBA", (sum(i.width for i in ims), H), "white")
    x = 0
    for i in ims:
        c.paste(i, (x, 0)); x += i.width
    return c


src = scene()
strip([run(src, "point", light=0, lx=100, ly=40, length=150, soft=8),
       run(src, "dir", light=1, angle=30, length=200, fade=0, opacity=1, soft=0, color="0.2,0.1,0.4")]).save(os.path.join(OUT, "01_point_and_directional.png"))
strip([run(src, "fade", light=1, angle=45, length=80, fade=1, soft=20, opacity=0.8),
       run(src, "start", light=1, angle=60, length=120, start=25, fade=0.5, soft=6, opacity=0.6)]).save(os.path.join(OUT, "02_soft_and_offset.png"))
frames = []
for i in range(30):
    a = 2 * math.pi * i / 30
    lx, ly = int(240 + 200 * math.cos(a)), int(200 + 160 * math.sin(a))
    frames.append(run(src, f"a{i}", light=0, lx=lx, ly=ly, length=140, soft=6).convert("RGB"))
frames[0].save(os.path.join(OUT, "03_moving_light.gif"), save_all=True, append_images=frames[1:], duration=60, loop=0)
