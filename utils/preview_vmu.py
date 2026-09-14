"""Render VMU Manager's native XML layout with illustrative runtime data.

This is a layout preview, not a console screenshot or an SDL emulator.
Usage: python utils/preview_vmu.py --output /tmp/vmu-preview
"""
from pathlib import Path
import argparse
import xml.etree.ElementTree as E
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'applications/vmu_manager'
SCALE = 2


class Preview:
    def __init__(self, page):
        self.tree = E.parse(APP / 'app.xml').getroot()
        self.page = page
        self.resources = {e.get('name'): e for e in self.tree.find('resources') if e.get('name')}
        self.fonts = {e.get('name'): ImageFont.truetype(str(ROOT / 'resources/fonts/ttf/arial_lite.ttf'),
                        int(e.get('size')) * SCALE) for e in self.tree.find('resources') if e.tag == 'font'}
        self.image = Image.new('RGB', (640 * SCALE, 480 * SCALE), '#101923')
        self.draw = ImageDraw.Draw(self.image)
        self.overflows = []
        self.text = {
            'A1': 'A1 / Ready', 'B1': 'B1 / Ready',
            'name-device': 'A1', 'free-mem': 'free 124 blocks',
        }
        self.disabled = {'A2', 'B2', 'C1', 'C2', 'D1', 'D2'}
        self.hidden = {'progressbar_container', 'confirm'}
        self.focus = 'A1'
        if page == 'home':
            self.disabled.add('home_but')
            self.text['controls'] = 'D-pad: move   A: open VMU   B: back'
        else:
            self.text.update({
                'subtitle': 'Select a save. X / Copy sends it to the opposite location.',
                'left-title': 'VMU A1', 'left-path': '124 blocks free',
                'right-title': 'OTHER LOCATION', 'right-path': '/sd/VMU-backups',
                'save-name': 'SONICADV_000', 'save-size': '10  Block(s)',
                'desc-short': 'SONIC ADVENTURE', 'desc-long': 'Main game save',
                'copy-button': 'Copy to other >',
                'controls': 'D-pad: move   A: select/open   B: back   X: copy   Y: actions',
            })
            self.hidden.update({'/sd', '/ide', '/pc', '/cd', 'dst-vmu', 'location-help'})
            self.focus = 'file_browser'
        if page == 'locations':
            self.hidden.difference_update({'/sd', '/ide', '/pc', '/cd', 'dst-vmu', 'location-help'})
            self.hidden.add('file_browser2')
            self.disabled.update({'/ide', '/pc', '/cd', 'copy-button', 'dump-button'})
            self.text.update({'right-title': 'CHOOSE A LOCATION', 'right-path': 'SD / IDE / PC / disc / VMU'})
            self.focus = '/sd'
        if page == 'tools':
            self.text['subtitle'] = 'Choose an action for the selected save or VMU.'
            self.text['controls'] = 'D-pad: move   A: select   B: back'
            self.focus = 'delete-button'
        if page == 'folder':
            self.text['subtitle'] = 'Create a folder in the location you are browsing.'
            self.text['controls'] = 'D-pad: move   A: select   B: back'
            self.focus = 'folder-name'
        if page == 'confirm':
            self.hidden.remove('confirm')
            self.hidden.add('modal-browse')
            self.text.update({'confirm-text': 'Delete /vmu/A1/SONICADV_000', 'modal-accept': 'A  Delete'})

    def rect(self, x, y, w, h, color):
        self.draw.rectangle((x*SCALE, y*SCALE, (x+w)*SCALE-1, (y+h)*SCALE-1), fill=color)

    def surface(self, name, x, y):
        e = self.resources.get(name)
        if e is None:
            return
        if e.tag == 'surface':
            for fill in e:
                self.rect(x+int(fill.get('x',0)), y+int(fill.get('y',0)),
                          int(fill.get('width',e.get('width'))), int(fill.get('height',e.get('height'))), fill.get('color'))
        elif e.tag == 'image':
            image = Image.open(APP / e.get('src')).convert('RGBA')
            image = image.resize((image.width*SCALE, image.height*SCALE), Image.Resampling.NEAREST)
            self.image.paste(image, (x*SCALE,y*SCALE), image)

    def label(self, text, x, y, w, h, font='body', color='#E7EEF4', center=False, name=''):
        f = self.fonts[font]
        width = self.draw.textlength(text, font=f)
        if width > w*SCALE:
            self.overflows.append((name, text, round(width/SCALE), w))
        # SDL labels clip to their own rectangles; emulate the same constraint.
        layer = Image.new('RGBA', (max(1,w*SCALE), max(1,h*SCALE)))
        draw = ImageDraw.Draw(layer)
        box = draw.textbbox((0,0), text, font=f)
        draw.text(((w*SCALE-width)/2 if center else 0,
                   (h*SCALE-(box[3]-box[1]))/2-box[1]), text, font=f, fill=color)
        self.image.paste(layer, (x*SCALE,y*SCALE), layer)

    def render(self, e, ox=0, oy=0):
        name = e.get('name','')
        if name in self.hidden:
            return
        x, y = ox+int(e.get('x',0)), oy+int(e.get('y',0))
        w, h = int(e.get('width',0)), int(e.get('height',0))
        if e.tag == 'cardstack':
            index = 0 if self.page=='home' else 2 if self.page=='folder' else 3 if self.page=='tools' else 1
            self.render(list(e)[index],x,y)
            return
        if e.get('background'):
            self.surface(e.get('background'),x,y)
        if e.tag == 'label':
            color = '#53E1E3' if name=='left-title' and self.focus=='file_browser' else e.get('color','#E7EEF4')
            self.label(self.text.get(name,e.get('text','')), x,y,w,h,e.get('font','body'),color,name=name)
        elif e.tag=='input' and e.get('type')=='button':
            state = 'disabled' if name in self.disabled else 'highlight' if name==self.focus else 'normal'
            self.surface(e.get(state),x,y)
            caption = e.find('label')
            self.label(self.text.get(name,caption.get('text','')),x,y,w,h,caption.get('font'),caption.get('color'),True,name)
        elif e.tag=='input':
            self.surface(e.get('highlight') if name==self.focus else e.get('normal'),x,y)
            self.label(e.get('value',''),x+4,y,w-8,h,'body',name=name)
        elif e.tag=='image':
            self.surface(e.get('src'),x,y)
        elif e.tag=='filemanager':
            left = name=='file_browser'
            rows = ['..','SONICADV_000','ARCADIA_0000','EGG_DATA','SONICADV_001','ICONDATA_VMS'] if left else [
                '..','SONICADV_000.vms','ARCADIA_0000.vms','VMU_A1_001.vmd','VMU_A1_002.vmd']
            for i, row in enumerate(rows):
                self.surface('item-selected' if left and i==1 else e.get('item_normal'),x,y+i*26)
                self.label(row,x+4,y+i*26,w-26,26,'small','#EDF5FC',name='sample-row')
        else:
            for child in e:
                self.render(child,x,y)

    def save(self, path):
        self.render(self.tree.find('body'))
        self.image.save(path)
        return self.overflows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    issues=[]
    for page in ['home','saves','locations','tools','folder','confirm']:
        issues.extend(Preview(page).save(args.output/f'vmu-{page}.png'))
    for issue in sorted(set(issues)):
        print('Text exceeds its label:',issue)
    if issues:
        raise SystemExit(1)
    print('Rendered six native XML layout previews; text widths fit.')


if __name__=='__main__':
    main()
