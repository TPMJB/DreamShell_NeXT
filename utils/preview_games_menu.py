#!/usr/bin/env python3
"""Illustrative Games layout preview, using production geometry and bundled font."""
from pathlib import Path
import argparse, json, re, subprocess, tempfile
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
NAVY='#080f23'; PANEL='#121a31'; CYAN='#2ed4f0'; WHITE='#edf5fc'; MUTED='#91adc4'
def geometry():
    header=ROOT/'applications/games_menu/modules/next_layout.h'
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'layout.c';exe=Path(tmp)/'layout'
        c.write_text('#include <stdio.h>\nenum {MT_PLANE_TEXT=1,MT_IMAGE_TEXT_64_5X2,MT_IMAGE_128_4X3};\nenum {KeyStart,KeyCancel,KeyUp,KeyDown,KeyLeft,KeyRight};\n#include "'+str(header)+'"\n'+r'''
int main(void) {
 for(int mode=0;mode<=3;++mode) {
  int count=mode?NextRows(mode)*NextColumns(mode):NEXT_ACTION_COUNT;
  for(int i=0;i<count;++i) {
   NextRect r=mode?NextGameRect(mode,i):next_actions[i];
   printf("%d %d %d %d %d %d\n",mode,i,r.x,r.y,r.w,r.h);
  }
 }
}''')
        subprocess.run(['cc',str(c),'-o',str(exe)],check=True)
        rows=subprocess.check_output([str(exe)],text=True)
    return {m:[tuple(map(int,l.split()[2:])) for l in rows.splitlines() if int(l.split()[0])==m] for m in range(4)}
def render(mode,rects):
    im=Image.new('RGB',(640,480),NAVY);d=ImageDraw.Draw(im)
    font_path=ROOT/'resources/fonts/ttf/arial_lite.ttf'
    def text(s,x,y,size=14,color=WHITE,width=None):
        f=ImageFont.truetype(str(font_path),size)
        if width:
            while s and d.textlength(s,font=f)>width: s=s[:-1]
        d.text((x,y),s,font=f,fill=color,anchor='ls')
    def box(r,fill=PANEL,outline=None):
        x,y,w,h=r;d.rectangle((x,y,x+w-1,y+h-1),fill=fill,outline=outline,width=2 if outline else 1)
    text('K-UI / KATANA USER INTERFACE',24,28,12,CYAN);text('Games',24,57,28)
    text('TPMJB',535,33,21,CYAN);text('github.com/TPMJB',474,54,12,MUTED)
    text('Resident Evil - Code Veronica',24,80,17,width=520);text('GDI',564,80,12,CYAN)
    box((24,88,592,2),CYAN);box((24,408,592,1))
    games=['Resident Evil - Code Veronica','Evolution 2','Sonic Adventure','Crazy Taxi','Shenmue','Soulcalibur','Jet Set Radio','Power Stone']
    gd=Image.open(ROOT/'applications/games_menu/images/gd.png').convert('RGB')
    if mode==1:
        box((24,96,316,288));box((356,96,260,288))
        im.paste(gd.resize((256,256)),(358,108))
    else:box((24,96,592,296))
    for i,r in enumerate(rects[mode]):
        x,y,w,h=r
        if i==0:box(r,'#173849',CYAN)
        if mode==1:text(games[i],x+12,y+25,18,CYAN if i==0 else WHITE,w-24)
        elif mode==2:
            im.paste(gd.resize((64,64)),(x+4,y+3));text(games[i],x+74,y+41,16,CYAN if i==0 else WHITE,210)
        else:
            im.paste(gd.resize((128,128)),(x+32,y));text(games[i],x+8,y+144,14,CYAN if i==0 else WHITE,176)
    text(f'24 games  |  Page 1 / {3 if mode<3 else 4}  |  '+['','List','Compact','Gallery'][mode],24,402,13,MUTED)
    for i,(name,r) in enumerate(zip(['Play','Game setup','Scan artwork','View','Settings','Exit'],rects[0])):
        box(r,CYAN if i==2 else PANEL);text(name,r[0]+10,r[1]+24,14,NAVY if i==2 else WHITE,r[2]-18)
    text('Left/Right Choose action   A Select   B Return to games',24,470,12,MUTED,width=592)
    return im
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);rects=geometry()
    previews=[render(m,rects) for m in (1,2,3)]
    sheet=Image.new('RGB',(1280,990),NAVY)
    for i,p in enumerate(previews):sheet.paste(p,(i%2*640,i//2*510))
    d=ImageDraw.Draw(sheet);f=ImageFont.truetype(str(ROOT/'resources/fonts/ttf/arial_lite.ttf'),19)
    for i,name in enumerate(['List + preview','Compact covers','Gallery']):d.text((24+i%2*640,481+i//2*510),name,font=f,fill=MUTED)
    d.text((664,590),'Games 1.0.3  /  K-UI by TPMJB',font=f,fill=CYAN)
    d.text((664,635),'Illustrative layout preview\nBundled placeholders shown\nScan artwork stays visible in every view',font=f,fill=WHITE,spacing=12)
    sheet.save(args.output/'Games-Preview.png')
    print('Games layout preview:',args.output/'Games-Preview.png')
