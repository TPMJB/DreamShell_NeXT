"""Render the native XML main view with illustrative data, not a console capture."""
import argparse
import base64
import io
from pathlib import Path
import xml.etree.ElementTree as E
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/"applications/iso_loader"

class Preview:
    def __init__(self, modal=False):
        self.root=E.parse(APP/"app.xml").getroot()
        self.resources={e.get("name"):e for e in self.root.find("resources") if e.get("name")}
        self.fonts={}
        for e in self.root.find("resources"):
            if e.tag=="font":
                path=ROOT/"resources/fonts/ttf"/Path(e.get("src")).name
                self.fonts[e.get("name")]=ImageFont.truetype(str(path),int(e.get("size")))
        self.image=Image.new("RGBA",(640,480),"#101923")
        self.modal=modal
        self.text={"game_title":"Evolution 2","game_title2":" ",
                   "launch-summary":"sd | Baseline (unsaved)",
                   "launch-status":"Ready. Left/right: actions   Start: play   X: check   Y: settings",
                   "version":"v2.0.0",
                   "message-0":"Executable read check passed.",
                   "message-1":"Play compares the standalone loader's read before launch.",
                   "message-2":"This checks executable data, not game compatibility."}
        self.overflows=[]

    def surface(self, name, w, h):
        out=Image.new("RGBA",(max(w,1),max(h,1)))
        e=self.resources.get(name)
        if e is None:
            if name and name.startswith("#"):
                ImageDraw.Draw(out).rectangle((0,0,w,h),fill=name)
            return out
        if e.tag=="image":
            im=Image.open(APP/e.get("src")).convert("RGBA")
            out.alpha_composite(im,(0,0))
        elif e.tag=="surface":
            d=ImageDraw.Draw(out)
            for fill in e:
                if fill.tag=="fill":
                    x,y=int(fill.get("x",0)),int(fill.get("y",0))
                    fw,fh=int(fill.get("width",e.get("width",w))),int(fill.get("height",e.get("height",h)))
                    d.rectangle((x,y,x+fw-1,y+fh-1),fill=fill.get("color"))
        return out

    @staticmethod
    def dim(value, parent):
        return round(float(value[:-1])*parent/100) if value.endswith("%") else int(value)

    def render(self,e,ox=0,oy=0,pw=640,ph=480):
        name=e.get("name","")
        if name=="message-panel" and not self.modal: return
        x=ox+self.dim(e.get("x","0"),pw); y=oy+self.dim(e.get("y","0"),ph)
        w=self.dim(e.get("width",str(pw)),pw); h=self.dim(e.get("height",str(ph)),ph)
        if e.tag=="cardstack":
            self.render(list(e)[0],x,y,w,h); return
        layer=self.surface(e.get("background"),w,h)
        if e.tag=="input" and e.get("type")=="button":
            layer=self.surface(e.get("normal"),w,h)
        elif e.tag=="input" and e.get("type")=="checkbox":
            d=ImageDraw.Draw(layer)
            d.rectangle((1,3,18,20),fill="#233C50",outline="#53E1E3")
            if e.get("checked")=="true": d.line((4,11,8,16,15,6),fill="#53E1E3",width=2)
        elif e.tag=="image":
            layer=self.surface(e.get("src"),w,h)
        self.image.alpha_composite(layer,(x,y))
        if e.tag=="label":
            text=self.text.get(name,e.get("text",""))
            f=self.fonts[e.get("font","arial")]
            d=ImageDraw.Draw(self.image)
            tw=d.textlength(text,font=f); box=d.textbbox((0,0),text,font=f)
            if tw>w: self.overflows.append((name,text,round(tw,1),w))
            dx=(w-tw)/2 if e.get("align")=="center" else w-tw if e.get("align")=="right" else 0
            d.text((x+dx,y+(h-box[3]+box[1])/2-box[1]),text,font=f,fill=e.get("color","#EDF5FC"))
        elif e.tag=="filemanager":
            d=ImageDraw.Draw(self.image); f=self.fonts["arial"]
            rows=["..","Evolution 2","Sonic Adventure","Crazy Taxi","Homebrew","Utilities"]
            for i,text in enumerate(rows):
                self.image.alpha_composite(self.surface("item-selected" if i==1 else "item-normal",w-18,30),(x,y+i*30))
                d.text((x+9,y+i*30+7),text,font=f,fill="#EDF5FC")
            d.rectangle((x+w-18,y,x+w-1,y+h-1),fill="#192A39")
            d.rectangle((x+w-16,y+2,x+w-3,y+58),fill="#53E1E3")
            return
        for child in e:
            self.render(child,x,y,w,h)

    def save(self,path,emit=False):
        self.render(self.root.find("body"))
        if self.overflows:
            raise RuntimeError("Preview text exceeds native label bounds: "+repr(self.overflows))
        self.image.convert("RGB").save(path)
        if emit:
            buf=io.BytesIO(); self.image.convert("RGB").save(buf,format="PNG")
            print("ISO_PREVIEW_BASE64:"+base64.b64encode(buf.getvalue()).decode())

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path,default=Path("/tmp/iso-loader-preview"))
    parser.add_argument("--emit",action="store_true")
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    Preview().save(args.output/"ISO-Loader-Preview.png",args.emit)
    Preview(modal=True).save(args.output/"ISO-Loader-Check-Preview.png")
