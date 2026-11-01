# 「空白をレイヤーの形に合わせる」と「手描き風のがたつき」のサンプル画像を previews/ に書き出す
# 使い方: g++ -O2 -std=c++17 -o <作業フォルダ>/preview tools/preview.cpp && python3 tools/make_shape_wobble_samples.py <作業フォルダ>
import os, subprocess, sys
from PIL import Image, ImageDraw
SP = sys.argv[1]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'previews') + '/'
W, H = 960, 540

def character():
    # おばけ + 箱のような、楕円では合わないシルエット
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((420, 150, 540, 330), radius=60, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255), width=4)
    d.polygon([(380, 280), (570, 280), (580, 380), (390, 380)], fill=(220, 40, 40, 255), outline=(0, 0, 0, 255))
    d.ellipse((455, 210, 470, 225), fill=(0, 0, 0, 255)); d.ellipse((480, 210, 495, 225), fill=(0, 0, 0, 255))
    return im

def render(img, args):
    open(SP + '/in.rgba', 'wb').write(img.tobytes())
    subprocess.check_call([SP + '/preview', SP + '/in.rgba', SP + '/out.rgba', str(W), str(H)] + args)
    o = Image.frombytes('RGBA', (W, H), open(SP + '/out.rgba', 'rb').read())
    return Image.alpha_composite(Image.new('RGBA', (W, H), (255, 255, 255, 255)), o).convert('RGB')

def pair(a, b, la, lb, name):
    g = Image.new('RGB', (W, H * 2 + 56), (255, 255, 255))
    d = ImageDraw.Draw(g)
    d.text((10, 8), la, fill=(0, 0, 0)); g.paste(a, (0, 28))
    d.text((10, H + 36), lb, fill=(0, 0, 0)); g.paste(b, (0, H + 56))
    g.save(OUT + name)

ch = character()
common = ['cy=270', 'count=200', 'width=7', 'mode=1', 'color=D82828', 'taper=0.5', 'stj=0.15']
pair(render(ch, common + ['rx=150', 'ry=150']),
     render(ch, common + ['shape=1', 'margin=25', 'smooth=0.05']),
     'Inner Shape: Ellipse', 'Inner Shape: Layer Silhouette (margin 25px)', 'shape_comparison.png')
pair(render(ch, common + ['shape=1', 'margin=25']),
     render(ch, common + ['shape=1', 'margin=25', 'wobble=2.5', 'wobblesize=90', 'uneven=0.35', 'stj=0.4']),
     'Wobble 0', 'Wobble 2.5px / Unevenness 35%', 'wobble_comparison.png')
