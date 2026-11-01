# 「外端ほど濃く」の前後比較画像を previews/edge_density_comparison.png に書き出す
# 使い方: python3 tools/make_edge_density_samples.py <変更前の preview> <変更後の preview> <作業フォルダ>
import os, subprocess, sys
from PIL import Image, ImageDraw
OLD, NEW, SP = sys.argv[1:4]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'previews') + '/'
W, H = 960, 540

def render(exe, args):
    open(SP + '/in.rgba', 'wb').write(Image.new('RGBA', (W, H), (255, 255, 255, 255)).tobytes())
    subprocess.check_call([exe, SP + '/in.rgba', SP + '/out.rgba', str(W), str(H), 'rx=150', 'ry=100', 'count=200', 'width=6'] + args)
    return Image.frombytes('RGBA', (W, H), open(SP + '/out.rgba', 'rb').read()).convert('RGB')

panels = [('Before (Taper 30%, Line Width 6)', OLD, ['taper=0.3']),
          ('After (same settings)', NEW, ['taper=0.3']),
          ('After (Line Width 11)', NEW, ['taper=0.3', 'width=11'])]
g = Image.new('RGB', (W, (H + 28) * len(panels)), (255, 255, 255))
d = ImageDraw.Draw(g)
for i, (label, exe, a) in enumerate(panels):
    y = i * (H + 28)
    d.text((10, y + 8), label, fill=(0, 0, 0)); g.paste(render(exe, a), (0, y + 28))
g.save(OUT + 'edge_density_comparison.png')
