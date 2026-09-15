#!/usr/bin/env python3
"""Render shipped utility XML/fonts with sample runtime data (not emulation)."""
from pathlib import Path
import xml.etree.ElementTree as E
from PIL import Image,ImageDraw,ImageFont
import sys
ROOT=Path(__file__).resolve().parents[1]
S=2
RESAMPLE=getattr(Image,'Resampling',Image).LANCZOS

class Preview:
    def __init__(self,app,values,focus='',hidden=(),active_tab=None):
        self.app=app;self.path=ROOT/'applications'/app
        self.xml=E.parse(self.path/'app.xml').getroot()
        self.resources={e.get('name'):e for e in self.xml.find('resources') if e.get('name')}
        self.fonts={e.get('name'):ImageFont.truetype(str(ROOT/'resources/fonts/ttf/arial_lite.ttf'),int(e.get('size'))*S) for e in self.xml.find('resources') if e.tag=='font'}
        self.values=values;self.focus=focus;self.hidden=set(hidden);self.overflows=[]
        if active_tab is not None:
            for i in range(5):
                tab=self.xml.find(f"body/input[@name='tab-{i}']")
                for state in ('normal','highlight','pressed'):
                    tab.set(state,f'tab-active-{state}' if i==active_tab else f'b108x30-{state}')
                tab.find('label').set('color','#101923' if i==active_tab else '#EFF5FC')
        self.canvas=Image.new('RGB',(640*S,480*S),'#101923');self.draw=ImageDraw.Draw(self.canvas)
    def rect(self,x,y,w,h,color):
        self.draw.rectangle((x*S,y*S,(x+w)*S-1,(y+h)*S-1),fill=color)
    def surface(self,name,x,y,w,h):
        if name.startswith('#'): self.rect(x,y,w,h,name);return
        surface=self.resources.get(name)
        if surface is None:return
        if surface.tag=='surface':
            for fill in surface:
                fx,fy=int(fill.get('x',0)),int(fill.get('y',0))
                fw,fh=min(w-fx,int(fill.get('width',surface.get('width')))),min(h-fy,int(fill.get('height',surface.get('height'))))
                if fw>0 and fh>0:self.rect(x+fx,y+fy,fw,fh,fill.get('color'))
        elif surface.tag=='image':
            with Image.open(self.path/surface.get('src')) as image:
                image=image.convert('RGBA').resize((w*S,h*S),RESAMPLE)
                self.canvas.paste(image,(x*S,y*S),image)
    def text(self,name,text,x,y,w,h,font='body',color='#EFF5FC'):
        f=self.fonts[font];width=self.draw.textlength(text,font=f)
        if width>w*S:self.overflows.append((name,text,round(width/S),w))
        layer=Image.new('RGBA',(w*S,h*S));d=ImageDraw.Draw(layer)
        box=d.textbbox((0,0),text,font=f)
        d.text((0,(h*S-(box[3]-box[1]))/2-box[1]),text,font=f,fill=color)
        self.canvas.paste(layer,(x*S,y*S),layer)
    def render(self,e,ox=0,oy=0):
        name=e.get('name','')
        if name in self.hidden or e.tag=='dialog':return
        x,y=ox+int(e.get('x',0)),oy+int(e.get('y',0));w,h=int(e.get('width',640)),int(e.get('height',480))
        if e.get('background'):self.surface(e.get('background'),x,y,w,h)
        if e.tag=='input':self.surface(e.get('highlight' if name==self.focus else 'normal'),x,y,w,h)
        if e.tag=='image':self.surface(e.get('src'),x,y,w,h)
        if e.tag=='label':self.text(name,self.values.get(name,e.get('text','')),x,y,w,h,e.get('font','body'),e.get('color','#EFF5FC'))
        if e.tag=='filemanager':
            entries=self.values.get(name,[])
            for i,text in enumerate(entries):
                self.rect(x,y+i*28,w-20,28,'#155B71' if i==2 and name=='filemgr-top' else '#182838')
                self.text(name,text,x+6,y+i*28,w-32,28,'small')
            self.rect(x+w-18,y+2,14,h-4,'#263749')
            self.rect(x+w-16,y+22,10,50,'#527080')
        for c in e:self.render(c,x,y)
    def save(self,path):
        self.render(self.xml.find('body'));self.canvas.save(path)
        if self.overflows:print(self.app,self.overflows)
        return self.canvas

def main(folder):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    filemanager=Preview('filemanager',{
        'path-top-caption':'> /sd/Games','path-bottom-caption':'  /sd/Backups',
        'filemgr-top':['[..]','[Evolution 2]','[Resident Evil CV]','[Skies of Arcadia]','[Sonic Adventure]','[Time Stalkers]'],
        'filemgr-bottom':['[..]','[VMU saves]','[Disc dumps]'],
        'title':'Resident Evil CV  /  Folder'},focus='copy-btn').save(folder/'file-manager.png')
    gdplay=Preview('gdplay',{'title1-txt':'RESIDENT EVIL','title2-txt':'CODE: Veronica','disc-type':'GD-ROM','disc-state':'Ready to play',
        'region-txt':'USA','vga-txt':'Yes','date-txt':'2000-02-03','disk-num-txt':'1/2','version-txt':'V1.000','product-txt':'T-1201N',
        'status':'A plays the disc. B returns to the menu. DreamShell closes when a game starts.'},focus='play-btn').save(folder/'gd-play.png')
    pages=[['Native output:  Auto (detect cable)','Screen shape:  4:3 (640 x 480)','Scaling filter:  Sharp (nearest)'],
        ['Volume:  90%','Menu sounds:  On','Button click:  On','Selection sound:  Off','Startup chime:  On'],
        ['Start in:  Launch App','Return to:  Launch App','Resources:  Auto (recommended)','Startup script:  /lua/startup.lua'],
        ['Year:  2026','Month:  09','Day:  14','Hour (local):  21','Minute:  30','Network sync time zone:  UTC-05:00','Set console clock:  Apply edited time'],
        ['Connect at startup:  Ethernet','Sync clock at startup:  On','Network configuration:  Open Network app','Devices:  SD yes / IDE no / VMU yes','Running from:  /sd/DS','Restore defaults:  Review before saving','Restart DreamShell']]
    for index,rows in enumerate(pages):
        values={f'row-{i}-caption':text for i,text in enumerate(rows)}
        values['heading']='Settings / '+['Display','Sound','Startup','Clock','System'][index]
        values['help']=['Native output applies after restart. Auto detects your video cable.',
            'Muting preserves your individual sound choices. Save to apply.',
            'Startup changes apply after restart. Resources must be mounted.',
            'Clock fields use local time. Time zone applies to network clock sync.',
            'Clock sync needs a connection. Select Devices to refresh detection.'][index]
        values['save-status']='Unsaved changes  /  START or Save settings to keep them'
        canvas=Preview('settings',values,focus='row-0',hidden={f'row-{i}' for i in range(len(rows),7)},active_tab=index).save(folder/f'settings-{index}.png')
        if index==0:settings=canvas
    icons=Image.new('RGB',(1280,960),'#101923');d=ImageDraw.Draw(icons)
    font=ImageFont.truetype(str(ROOT/'resources/fonts/ttf/arial_lite.ttf'),24)
    titlefont=ImageFont.truetype(str(ROOT/'resources/fonts/ttf/arial_lite.ttf'),44)
    d.text((48,38),'NeXT icon family',font=titlefont,fill='#EFF5FC')
    d.text((48,102),'Original vector symbols, native 64 x 64 launcher textures.',font=font,fill='#A7B9CB')
    apps=['games_menu','iso_loader','gdplay','gd_ripper','filemanager','vmu_manager','settings','network','bios_flasher','region_changer','dreameye','speedtest']
    for i,app in enumerate(apps):
        path=ROOT/'applications'/app;root=E.parse(path/'app.xml').getroot()
        x,y=60+(i%4)*306,186+(i//4)*244
        with Image.open(path/root.get('icon')) as im:
            im=im.convert('RGBA').resize((128,128),RESAMPLE);icons.paste(im,(x,y),im)
        d.text((x,y+144),root.get('name'),font=font,fill='#EFF5FC')
    icons.save(folder/'icons.png')
    sheet=Image.new('RGB',(2560,1920),'#101923')
    for image,pos in [(icons,(0,0)),(filemanager,(1280,0)),(gdplay,(0,960)),(settings,(1280,960))]:sheet.paste(image,pos)
    sheet.save(folder/'utility-apps-preview.png')
if __name__=='__main__': main(sys.argv[1])
