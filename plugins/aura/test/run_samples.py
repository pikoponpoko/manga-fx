#!/usr/bin/env python3
"""Builds aura_test, renders sample images and writes PNG previews.

usage: python3 run_samples.py <work_dir> <out_dir>
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
work, out = sys.argv[1], sys.argv[2]
os.makedirs(work, exist_ok=True)
os.makedirs(out, exist_ok=True)

exe = os.path.join(work, "aura_test")
subprocess.check_call(["g++", "-O2", "-std=c++17", "-pthread", "-Wall", "-Wextra",
                       os.path.join(HERE, "aura_test.cpp"), os.path.join(HERE, "..", "src", "AuraCore.cpp"),
                       "-o", exe])

W, H = 420, 520


def figure(draw, fill, outline=None, width=0):
    kw = dict(fill=fill, outline=outline, width=width)
    draw.ellipse((150, 40, 270, 160), **kw)                       # head
    draw.polygon([(160, 70), (140, 20), (190, 50), (210, 5), (230, 50), (280, 20), (260, 75)], **kw)  # hair
    draw.rounded_rectangle((145, 165, 275, 350), radius=40, **kw)  # body
    draw.polygon([(150, 190), (60, 120), (45, 140), (140, 240)], **kw)    # raised arm
    draw.polygon([(270, 190), (350, 290), (330, 305), (265, 245)], **kw)  # lower arm
    draw.polygon([(160, 330), (130, 500), (175, 500), (205, 340)], **kw)  # legs
    draw.polygon([(215, 340), (245, 500), (290, 500), (260, 330)], **kw)


# 1) Colour character with alpha
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
figure(ImageDraw.Draw(img), (240, 120, 90, 255))
img.save(os.path.join(work, "color.png"))

# 2) Binarised line art: black lines on opaque white
line = Image.new("RGBA", (W, H), (255, 255, 255, 255))
d = ImageDraw.Draw(line)
figure(d, None, (0, 0, 0, 255), 4)
d.line((180, 100, 195, 100), fill=(0, 0, 0, 255), width=4)  # eyes
d.line((225, 100, 240, 100), fill=(0, 0, 0, 255), width=4)
line.save(os.path.join(work, "lineart.png"))


def run(src, name, pad, args, bg=(18, 18, 28)):
    im = Image.open(os.path.join(work, src)).convert("RGBA")
    raw_in = os.path.join(work, src + ".rgba")
    raw_out = os.path.join(work, name + ".rgba")
    open(raw_in, "wb").write(im.tobytes())
    if not any(a.startswith("fillcomp=") for a in args):
        args = ["fillcomp=0"] + args   # samples show a finite Aura Length unless they say otherwise
    res = subprocess.check_output([exe, raw_in, str(im.width), str(im.height), raw_out, str(pad)] + args).decode().split()
    ow, oh = int(res[0]), int(res[1])
    o = Image.frombytes("RGBA", (ow, oh), open(raw_out, "rb").read())
    canvas = Image.new("RGBA", (ow, oh), bg + (255,))
    canvas.alpha_composite(o)
    canvas.convert("RGB").save(os.path.join(out, name + ".png"))
    return canvas


run("color.png", "01_color_basic", 200, ["spacing=18", "width=5", "length=400", "fade=250"])
run("color.png", "02_color_rainbow_wobble", 200,
    ["spacing=14", "width=4", "length=420", "fade=200", "color=3", "hue=25", "wobble=10", "wsize=90", "c1=ff4060"])
run("color.png", "03_color_gradient_taper", 200,
    ["spacing=24", "width=9", "outer=0.2", "soft=3", "length=420", "fade=150", "color=1", "glen=400", "c1=fff080", "c2=ff3080"])
run("lineart.png", "04_lineart_over", 0,
    ["source=1", "comp=2", "gaps=3", "spacing=14", "width=3", "length=300", "fade=150", "color=2", "c1=2060ff", "c2=ff4080"], bg=(255, 255, 255))
run("lineart.png", "05_lineart_nofill", 0,
    ["source=1", "comp=2", "gaps=3", "fill=0", "spacing=14", "width=3", "length=300", "fade=150", "c1=2060ff"], bg=(255, 255, 255))
run("color.png", "07_fill_gradient", 200,
    ["style=1", "start=0", "length=200", "fade=90", "soft=2", "smooth=10", "color=1", "glen=260",
     "c1=fff4a0", "c2=ff5020", "wobble=14", "wsize=60"])
run("color.png", "08_fill_alternate_bands", 200,
    ["style=1", "start=0", "spacing=28", "length=360", "fade=120", "soft=1.5", "color=2", "c1=40c8ff", "c2=1838a0"])
run("color.png", "09_fill_plus_lines", 200,
    ["style=2", "spacing=18", "width=3", "length=320", "fade=220", "fillc=2050ff", "fillop=0.55", "c1=c0f0ff"])
run("color.png", "10_corner_rounding_0", 200, ["spacing=18", "width=5", "length=400", "fade=250"])
run("color.png", "11_corner_rounding_40", 200, ["spacing=18", "width=5", "length=400", "fade=250", "round=40"])

# Color cycle: hue rotates once per second (loops seamlessly)
frames = []
for i in range(24):
    frames.append(run("color.png", "_cyc", 120,
                      ["style=2", "spacing=16", "width=4", "length=240", "fade=160", "round=25", "color=3", "hue=20",
                       "c1=ff4060", "fillc=401860", "fillop=0.6", "cycle=1", "cdur=1", f"time={i / 24.0:.4f}"]).convert("P", palette=Image.ADAPTIVE))
os.remove(os.path.join(out, "_cyc.png"))
frames[0].save(os.path.join(out, "12_color_cycle.gif"), save_all=True, append_images=frames[1:], duration=42, loop=0)

# Barber pole in rings: 3-colour palette stripes travelling outward, loops every second
frames = []
for i in range(24):
    frames.append(run("color.png", "_barber", 120,
                      ["style=1", "start=0", "spacing=14", "length=260", "fade=60", "soft=1", "round=20",
                       "color=2", "ncol=3", "c1=1f4fd0", "c2=e02030", "c3=ffffff",
                       "cycle=2", "cdur=1", f"time={i / 24.0:.4f}"]).convert("P", palette=Image.ADAPTIVE))
os.remove(os.path.join(out, "_barber.png"))
frames[0].save(os.path.join(out, "13_barber_pole.gif"), save_all=True, append_images=frames[1:], duration=42, loop=0)
run("color.png", "14_five_colors_lines", 200,
    ["spacing=12", "width=5", "length=360", "fade=150", "color=2", "ncol=5",
     "c1=ff3050", "c2=ffa020", "c3=fff060", "c4=40e080", "c5=40a0ff"])
z = run("color.png", "15_narrow_gaps_zoom", 0, ["spacing=6", "width=1.5", "length=200", "fade=0", "start=1"])
z.crop((190, 140, 330, 300)).resize((420, 480), Image.LANCZOS).convert("RGB").save(os.path.join(out, "15_narrow_gaps_zoom.png"))
run("color.png", "16_fill_composition_default", 300, ["fillcomp=1"])
run("color.png", "17_fill_stripes_clean_edge", 120,
    ["fillcomp=1", "style=1", "color=2", "ncol=5", "spacing=40"])

# Shape layer: aura on an empty background layer, following a character placed
# on another layer (moved, scaled and rotated).
bgw, bgh = 640, 480
open(os.path.join(work, "bg.rgba"), "wb").write(Image.new("RGBA", (bgw, bgh), (0, 0, 0, 0)).tobytes())
Image.new("RGBA", (bgw, bgh), (0, 0, 0, 0)).save(os.path.join(work, "bg.png"))
run("bg.png", "18_shape_layer", 0,
    ["fillcomp=1", "comp=1", "style=1", "color=2", "ncol=3", "spacing=24",
     "c1=ff4060", "c2=ffd040", "c3=40c0ff",
     "shape=" + os.path.join(work, "color.png.rgba"), f"sw={W}", f"sh={H}",
     "stx=110", "sty=-20", "ssc=0.7", "srot=15"])

# Retro Rings preset (Looney Tunes style): concentric circles with band shading, not cropped to a circle
run("color.png", "19_looney_circles", 200,
    ["fillcomp=1", "ring=1", f"cx={W / 2}", f"cy={H / 2 - 40}", "style=1", "start=0", "spacing=70",
     "color=0", "c1=e01818", "shading=1", "shadec=ffb020"])

# HSB Steps: rings stepped from Color 1, hard edges
run("color.png", "20_hsb_steps_lines", 200,
    ["fillcomp=1", "color=4", "c1=ff3050", "steps=6", "hshift=150", "bshift=-0.3", "hsbloop=3", "spacing=14", "width=7"])
run("color.png", "21_hsb_steps_fill", 200,
    ["fillcomp=1", "style=1", "color=4", "c1=2040ff", "steps=5", "hshift=0", "sshift=-0.8", "bshift=0.1", "spacing=22"])

# Animation: rings flowing outward
frames = []
for i in range(24):
    t = i / 24.0
    frames.append(run("color.png", "_anim", 160,
                      ["spacing=20", "width=6", "soft=2", "length=320", "fade=200", "speed=20", f"time={t:.4f}",
                       "wobble=6", "wspeed=1.5", "color=2", "c1=60d0ff", "c2=b050ff"]).convert("P", palette=Image.ADAPTIVE))
os.remove(os.path.join(out, "_anim.png"))
frames[0].save(os.path.join(out, "06_anim.gif"), save_all=True, append_images=frames[1:], duration=42, loop=0)
print("done")
