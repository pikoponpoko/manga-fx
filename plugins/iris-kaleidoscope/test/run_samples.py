#!/usr/bin/env python3
"""Builds kaleido_test, renders sample eyes and writes PNG / GIF previews.

usage: python3 run_samples.py <work_dir> <out_dir>
"""
import math
import os
import random
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
work, out = sys.argv[1], sys.argv[2]
os.makedirs(work, exist_ok=True)
os.makedirs(out, exist_ok=True)

exe = os.path.join(work, "kaleido_test")
subprocess.check_call(["g++", "-O2", "-std=c++17", "-pthread", "-Wall", "-Wextra",
                       os.path.join(HERE, "kaleido_test.cpp"), os.path.join(HERE, "..", "src", "KaleidoCore.cpp"),
                       "-o", exe])

W, H = 520, 380
SS = 3  # supersampling for the hand-drawn test art
EYES = [((150, 170), 92, 128, -28), ((380, 205), 78, 110, -28)]   # centre, semi-axes, tilt (deg)


def ellipse_poly(c, a, b, tilt, scale=1.0, n=180):
    t = math.radians(tilt)
    pts = []
    for i in range(n):
        u = 2 * math.pi * i / n
        x, y = a * math.cos(u), b * math.sin(u)
        pts.append(((c[0] + x * math.cos(t) - y * math.sin(t)) * scale, (c[1] + x * math.sin(t) + y * math.cos(t)) * scale))
    return pts


def slit(c, a, b, tilt, scale=1.0):
    return ellipse_poly(c, a * 0.09, b * 0.42, tilt, scale)


def draw_ss(fn, size=(W, H), mode="RGBA", bg=(0, 0, 0, 0)):
    im = Image.new(mode, (size[0] * SS, size[1] * SS), bg)
    fn(ImageDraw.Draw(im), SS)
    return im.resize(size, Image.LANCZOS)


# Eye fill layer (clean cel paint) and the line art that sits above it.
fill = draw_ss(lambda d, s: [d.polygon(ellipse_poly(c, a, b, t, s), fill=(70, 110, 200, 255)) for c, a, b, t in EYES])
fill.save(os.path.join(work, "fill.png"))


def lines_fn(d, s):
    for c, a, b, t in EYES:
        d.line(ellipse_poly(c, a, b, t, s) + [ellipse_poly(c, a, b, t, s)[0]], fill=(25, 20, 30, 255), width=4 * s)
        d.polygon(slit(c, a, b, t, s), fill=(25, 20, 30, 255))


lines = draw_ss(lines_fn)
lines.save(os.path.join(work, "lines.png"))

# Analog eye: scanned paper, watercolour-like iris with uneven edge, ink lines.
random.seed(3)
paper = Image.new("RGB", (W, H), (250, 250, 247))
noise = Image.effect_noise((W, H), 18).convert("L")
paper = Image.composite(paper, Image.new("RGB", (W, H), (232, 232, 228)), noise.point(lambda v: 255 if v > 110 else 200))


def wash_layer(d, s):
    for c, a, b, t in EYES:
        jx, jy = random.uniform(-4, 4), random.uniform(-4, 4)
        sh = random.uniform(-14, 14)
        d.polygon(ellipse_poly((c[0] + jx, c[1] + jy), a - 3, b - 3, t, s), fill=(55 + int(sh), 105 + int(sh), 190, 70))


wash = Image.new("RGBA", (W, H), (0, 0, 0, 0))
for _ in range(8):
    wash.alpha_composite(draw_ss(wash_layer))
grain = Image.effect_noise((W, H), 40).convert("L").filter(ImageFilter.GaussianBlur(1.2))
wash.putalpha(ImageChops.multiply(wash.getchannel("A"), grain.point(lambda v: min(255, 90 + v))))
analog = paper.convert("RGBA")
analog.alpha_composite(wash.filter(ImageFilter.GaussianBlur(0.8)))
analog.alpha_composite(lines)
analog.save(os.path.join(work, "analog.png"))

# Custom motif layer: a small flower with an ink outline.
def flower_fn(d, s):
    for i in range(5):
        a = math.radians(i * 72 - 90)
        cx, cy = 60 + 26 * math.cos(a), 60 + 26 * math.sin(a)
        d.ellipse(((cx - 20) * s, (cy - 20) * s, (cx + 20) * s, (cy + 20) * s), fill=(255, 150, 190, 255), outline=(120, 30, 70, 255), width=3 * s)
    d.ellipse((44 * s, 44 * s, 76 * s, 76 * s), fill=(255, 225, 90, 255), outline=(120, 30, 70, 255), width=3 * s)


flower = draw_ss(flower_fn, size=(120, 120))
flower.save(os.path.join(work, "flower.png"))


def to_raw(name):
    im = Image.open(os.path.join(work, name)).convert("RGBA")
    p = os.path.join(work, name + ".rgba")
    open(p, "wb").write(im.tobytes())
    return p, im.width, im.height


def run(src, args, pattern=None):
    raw_in, w, h = to_raw(src)
    raw_out = os.path.join(work, "out.rgba")
    if pattern:
        praw, pw, ph = to_raw(pattern)
    else:
        praw, pw, ph = "-", 0, 0
    subprocess.check_call([exe, raw_in, str(w), str(h), raw_out, praw, str(pw), str(ph)] + args)
    return Image.frombytes("RGBA", (w, h), open(raw_out, "rb").read())


SKIN = (255, 236, 228)


def on_face(eff):
    """Character composite: skin, effect on the eye fill, line art on top."""
    canvas = Image.new("RGBA", (W, H), SKIN + (255,))
    canvas.alpha_composite(eff)
    canvas.alpha_composite(lines)
    return canvas.convert("RGB")


def save(img, name):
    img.save(os.path.join(out, name + ".png"))
    return img


save(on_face(fill), "00_before")
save(on_face(run("fill.png", ["time=0.6"])), "01_default_petal")
save(on_face(run("fill.png", ["time=0.6", "overlay=1", "oc1=40d8ff", "oc2=b040ff", "fade=0.6", "blur=5"])), "02_tint_gradient_dof")
save(on_face(run("fill.png", ["time=0.3", "shape=3", "copies=30", "overlay=1", "oc1=ffd040", "oc2=ff3080", "wave=0.06", "wcount=8"])), "03_star_wavy")
save(on_face(run("fill.png", ["time=0.6", "copies=20", "size=0.95", "fade=0.35", "blur=3"], pattern="flower.png")), "04_custom_layer")
save(on_face(run("fill.png", ["time=0.6", "shape=1", "copies=36", "twist=25", "overlay=4", "oc1=ffffff", "oc2=40e0ff", "oamt=0.5",
                              "blend=2", "fade=0.2"])), "05_diamond_twist_screen")
save(run("analog.png", ["mask=1", "key=4a82c4", "tol=0.12", "ksoft=0.12", "feather=1.5", "time=0.6", "overlay=1",
                        "oc1=60e8ff", "oc2=8040ff", "oamt=0.85", "blend=0", "opacity=0.9", "blur=4"]).convert("RGB"), "06_analog_keycolor")
save(on_face(run("fill.png", ["time=0.6", "blur=10", "focus=1", "frange=0.1", "overlay=1", "oc1=ff9040", "oc2=ffe080"])), "07_focus_center")

save(on_face(run("fill.png", ["time=0.6", "copies=72", "blur=1", "fade=0.4", "overlay=1", "oc1=40d8ff", "oc2=b040ff"])), "09_fine_72copies")

# Animation: rotation + sway + waves (+ a slow inward flow).
frames = []
for i in range(36):
    t = i / 12.0
    frames.append(on_face(run("fill.png", [f"time={t}", "overlay=1", "oc1=40d8ff", "oc2=b040ff", "fade=0.55", "blur=4",
                                           "rspeed=30", "sway=12", "sspeed=0.33", "wave=0.05", "wspeed=0.33", "flow=0.33"])).quantize(128))
frames[0].save(os.path.join(out, "08_anim.gif"), save_all=True, append_images=frames[1:], duration=83, loop=0)
print("done")
