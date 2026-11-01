# 使い方: g++ -O2 -std=c++17 -o <作業フォルダ>/preview tools/preview.cpp && python3 tools/make_previews.py <作業フォルダ>
import subprocess, sys
from PIL import Image, ImageDraw
import numpy as np
SP=sys.argv[1]; import os; OUT=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'previews') + '/'
W,H=1280,720
# 1: color background (gradient) + character (opaque)
g=np.zeros((H,W,4),np.uint8)
yy,xx=np.mgrid[0:H,0:W]
g[...,0]=(80+120*xx/W).astype(np.uint8); g[...,1]=(150+80*yy/H).astype(np.uint8); g[...,2]=230; g[...,3]=255
color=Image.fromarray(g,'RGBA'); d=ImageDraw.Draw(color)
d.ellipse((520,180,760,420),fill=(250,210,180,255),outline=(60,30,20,255),width=6)
d.rectangle((560,420,720,720),fill=(220,60,80,255),outline=(60,30,20,255),width=6)
# 2: transparent character only
ch=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(ch)
d.ellipse((520,180,760,420),fill=(250,210,180,255),outline=(60,30,20,255),width=6)
d.rectangle((560,420,720,720),fill=(220,60,80,255),outline=(60,30,20,255),width=6)
# 3: white
white=Image.new('RGBA',(W,H),(255,255,255,255))
def run(img,name,args):
    img.save(SP+'/in.png'); open(SP+'/in.rgba','wb').write(img.tobytes())
    subprocess.check_call([SP+'/preview',SP+'/in.rgba',SP+'/out.rgba',str(W),str(H)]+args)
    o=Image.frombytes('RGBA',(W,H),open(SP+'/out.rgba','rb').read())
    if name.endswith('_alpha'):  # 透明部分を市松模様で見せる
        bg=Image.new('RGBA',(W,H),(255,255,255,255)); bd=ImageDraw.Draw(bg)
        for y in range(0,H,32):
            for x in range(0,W,32):
                if (x//32+y//32)%2: bd.rectangle((x,y,x+31,y+31),fill=(205,205,205,255))
        o=Image.alpha_composite(bg,o)
    o.convert('RGB').save(OUT+name+'.png')
run(white,'01_radial_basic',['rx=280','ry=180','count=240'])
run(color,'02_radial_color_over',['rx=200','ry=200','count=160','width=14','color=FFFFFF','cy=300'])
run(ch,'03_radial_behind_alpha',['rx=170','ry=230','count=300','width=6','mode=1','cy=360'])
run(white,'04_radial_dense_length',['rx=200','ry=120','count=600','width=4','length=260','lj=0.6','stj=0.8','angle=20'])
run(color,'05_parallel_color',['type=1','rx=220','ry=260','count=60','width=6','length=500','lj=0.5','angle=-15','color=FFFFFF','opacity=0.9','cy=400'])
run(ch,'06_parallel_lines_only_alpha',['type=1','rx=0','ry=0','count=40','width=5','length=0','angle=0','mode=2'])
