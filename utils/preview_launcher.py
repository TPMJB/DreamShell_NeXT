#!/usr/bin/env python3
"""Render the production launcher's recorded scene using its shipped TXF glyphs.

Requires gcc and Pillow. This is a layout preview, not Dreamcast emulation.
Usage: python3 utils/preview_launcher.py OUTPUT.png [SELECTED_INDEX]
"""
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'utils/tests'))
from test_launcher import LauncherTests


def render(output, index=0):
    LauncherTests.setUpClass()
    try:
        scene=json.loads(subprocess.check_output([str(LauncherTests.exe),'snapshot',str(index)],cwd=ROOT,text=True))
    finally:
        LauncherTests.tearDownClass()
    b=(ROOT/'resources/fonts/txf/helvetica.txf').read_bytes()
    _,_,fmt,w,h,ascent,_,count=struct.unpack('<4s7i', b[:32])
    assert fmt == 0
    glyphs={}
    atlas=ImageOps.flip(Image.frombytes('L',(w,h),b[32+count*12:]))
    for i in range(count):
        c,gw,gh,xo,yo,advance,pad,gx,gy=struct.unpack('<HBBbbbBHH',b[32+i*12:44+i*12])
        glyphs[c]=(atlas.crop((gx,h-gy-gh,gx+gw,h-gy)),xo,yo,gw,gh)
    scale=3
    canvas=Image.new('RGB',(640*scale,480*scale),'#101822')
    draw=ImageDraw.Draw(canvas)
    for d in sorted(scene,key=lambda d:d['z']):
        x,y=d['x']*scale,d['y']*scale
        color=tuple(round(c*255) for c in d['color'][:3])
        if d['kind']==0:
            draw.rounded_rectangle((x,y-d['h']*scale,x+d['w']*scale,y),radius=d['radius']*scale,fill=color)
        elif d['kind']==2 and d['image']:
            with Image.open(ROOT/d['image']) as im:
                im=im.convert('RGBA').resize((round(d['w']*scale),round(d['h']*scale)),getattr(Image,'Resampling',Image).LANCZOS)
                canvas.paste(im,(round(x-im.width/2),round(y-im.height/2)),im)
        elif d['kind']==1:
            size=d['size']*scale
            for c in d['text'].encode('utf8'):
                if c==32 or c not in glyphs:
                    x+=0.1*scale+size/2;continue
                mask,xo,yo,gw,gh=glyphs[c]
                factor=size/ascent
                if gw and gh:
                    mask=mask.resize((max(1,round(gw*factor)),max(1,round(gh*factor))),getattr(Image,'Resampling',Image).BILINEAR)
                    canvas.paste(color,(round(x+xo*factor),round(y-(yo+gh)*factor)),mask)
                x+=(0.1+(xo+gw)/ascent)*size
    # Static XML banners are not owned by the C scene harness. Render them
    # from the same shipped assets; coordinates are top-left in this XML API.
    for node in ET.parse(ROOT/'applications/launch_app/app.xml').findall('body/image'):
        with Image.open(ROOT/'applications/launch_app'/node.get('src')) as im:
            im = im.convert('RGB').resize((int(node.get('width'))*scale,int(node.get('height'))*scale),getattr(Image,'Resampling',Image).LANCZOS)
            canvas.paste(im,(int(node.get('x'))*scale,int(node.get('y'))*scale))
    canvas.resize((1280,960),getattr(Image,'Resampling',Image).LANCZOS).save(output)

if __name__=='__main__':
    render(sys.argv[1],int(sys.argv[2]) if len(sys.argv)>2 else 0)
