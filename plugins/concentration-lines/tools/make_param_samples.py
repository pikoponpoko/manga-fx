# パラメータ比較画像とアニメーション GIF を previews/ に書き出す
# 使い方: g++ -O2 -std=c++17 -o <作業フォルダ>/preview tools/preview.cpp && python3 tools/make_param_samples.py <作業フォルダ>
import subprocess, sys
from PIL import Image, ImageDraw
SP = sys.argv[1]
import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'previews') + '/'

def render(w, h, args, bg=(255, 255, 255, 255)):
    img = Image.new('RGBA', (w, h), bg)
    open(SP + '/in.rgba', 'wb').write(img.tobytes())
    subprocess.check_call([SP + '/preview', SP + '/in.rgba', SP + '/out.rgba', str(w), str(h)] + args)
    return Image.frombytes('RGBA', (w, h), open(SP + '/out.rgba', 'rb').read()).convert('RGB')

def grid(tiles, labels, cols, name):
    w, h = tiles[0].size
    rows = (len(tiles) + cols - 1) // cols
    g = Image.new('RGB', (w * cols, (h + 28) * rows), (255, 255, 255))
    d = ImageDraw.Draw(g)
    for i, (t, l) in enumerate(zip(tiles, labels)):
        x, y = (i % cols) * w, (i // cols) * (h + 28)
        g.paste(t, (x, y + 28))
        d.text((x + 8, y + 8), l, fill=(0, 0, 0))
    g.save(OUT + name)

W, H = 480, 300
base = ['rx=110', 'ry=70', 'count=120', 'width=7', ]

# 太さ・本数(間隔)・ばらつき・鋭さ の比較
cases = [
    ('Line Width 3', ['width=3']), ('Line Width 7 (default)', []), ('Line Width 14', ['width=14']),
    ('Line Count 60', ['count=60']), ('Line Count 120', []), ('Line Count 300', ['count=300']),
    ('Randomness 0% (uniform)', ['wj=0', 'sj=0', 'stj=0']), ('Randomness default', []),
    ('Randomness high', ['wj=0.8', 'sj=1', 'stj=1']),
    ('Sharpness 0% (blunt)', ['sharp=0']), ('Sharpness 50%', ['sharp=0.5']), ('Sharpness 100% (needle)', ['sharp=1']),
    ('Edge Softness 0', []), ('Edge Softness 3px', ['soft=3']), ('Edge Softness 8px', ['soft=8']),
]
grid([render(W, H, base + a) for _, a in cases], [l for l, _ in cases], 3, 'params_comparison.png')

# アニメーション: 集中線（ちらつき + 脈動）
fps = 12
frames = [render(400, 250, ['rx=90', 'ry=60', 'count=120', 'width=6', 'flicker=6',
                            'pulse=0.15', 'pulserate=1', 't=%f' % (i / fps)]) for i in range(fps * 2)]
frames[0].save(OUT + 'anim_flicker_pulse.gif', save_all=True, append_images=frames[1:], duration=1000 // fps, loop=0)

# アニメーション: 集中線の回転
frames = [render(400, 250, ['rx=90', 'ry=60', 'count=90', 'width=6', 'rot=30',
                            't=%f' % (i / fps)]) for i in range(fps * 2)]
frames[0].save(OUT + 'anim_rotation.gif', save_all=True, append_images=frames[1:], duration=1000 // fps, loop=0)

# アニメーション: 流線が流れる
frames = [render(400, 250, ['type=1', 'rx=80', 'ry=90', 'count=50', 'width=4', 'length=160', 'flow=-900', 't=%f' % (i / fps)]) for i in range(fps * 2)]
frames[0].save(OUT + 'anim_flow.gif', save_all=True, append_images=frames[1:], duration=1000 // fps, loop=0)
