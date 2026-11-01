# 自動追従（Layer Alpha Center）の見た目を確かめる GIF を作る。
# プラグインと同じく「キャラクターの不透明部分の重心」を中心にして描く。
# 使い方: python3 tools/make_follow_sample.py <preview をビルドした作業フォルダ>
import math, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw
SP = sys.argv[1]
import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'previews', 'anim_follow.gif')
W, H, FPS = 480, 270, 12
frames = []
for i in range(FPS * 2):
    t = i / FPS
    x = 120 + 240 * (0.5 - 0.5 * math.cos(math.pi * t))   # 左から右へゆっくり移動
    y = 150 + 25 * math.sin(2 * math.pi * t)
    ch = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(ch)
    d.ellipse((x - 30, y - 70, x + 30, y - 10), fill=(250, 210, 180, 255), outline=(60, 30, 20, 255), width=3)
    d.rectangle((x - 22, y - 10, x + 22, y + 70), fill=(220, 60, 80, 255), outline=(60, 30, 20, 255), width=3)
    a = np.asarray(ch)[..., 3].astype(float)
    ys, xs = np.mgrid[0:H, 0:W]
    cx, cy = (a * (xs + 0.5)).sum() / a.sum(), (a * (ys + 0.5)).sum() / a.sum()
    open(SP + '/in.rgba', 'wb').write(ch.tobytes())
    subprocess.check_call([SP + '/preview', SP + '/in.rgba', SP + '/out.rgba', str(W), str(H),
                           'cx=%f' % cx, 'cy=%f' % cy, 'rx=70', 'ry=95', 'count=140', 'width=6', 'mode=1',
                           'flicker=8', 't=%f' % t])
    o = Image.frombytes('RGBA', (W, H), open(SP + '/out.rgba', 'rb').read())
    frames.append(Image.alpha_composite(Image.new('RGBA', (W, H), (255, 255, 255, 255)), o).convert('RGB'))
frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=1000 // FPS, loop=0)
