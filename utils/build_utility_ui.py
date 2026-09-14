#!/usr/bin/env python3
"""Generate the NeXT utilities' explicit 640x480 SDL layouts and vector icons.

XML and SVG are the sources shipped with the project; CairoSVG rasterizes
the SVG icons to native 64x64 PNGs for the Dreamcast launcher.
"""
from pathlib import Path
import xml.etree.ElementTree as E

ROOT = Path(__file__).resolve().parents[1]
BG, PANEL, TEXT, MUTED, ACCENT = '#101923', '#1B2A39', '#EFF5FC', '#A7B9CB', '#53E1E3'

def node(parent, tag, **attrs):
    return E.SubElement(parent, tag, {k: str(v) for k, v in attrs.items()})

def label(p, name, text, x, y, w, h=24, font='body', color=TEXT):
    return node(p, 'label', name=name, text=text, x=x, y=y, width=w,
                height=h, font=font, color=color, align='left', valign='center')

def surface(r, name, w, h, color):
    s=node(r, 'surface', name=name, width=w, height=h)
    node(s, 'fill', color=color)
    return s

def button(p, r, name, text, x, y, w, h, onclick):
    stem=f'b{w}x{h}'
    if r.find(f"surface[@name='{stem}-normal']") is None:
        for state, color in [('normal','#223548'),('highlight',ACCENT),('pressed','#ACFFFF'),('disabled','#26313B')]:
            s=surface(r, f'{stem}-{state}',w,h,color)
            if state in ('highlight','pressed'):
                node(s,'fill',x=2,y=2,width=w-4,height=h-4,color='#155B71')
    b=node(p,'input',name=name,type='button',x=x,y=y,width=w,height=h,onclick=onclick,
           **{state:f'{stem}-{state}' for state in ('normal','highlight','pressed','disabled')})
    label(b,name+'-caption',text,12,0,w-24,h,'small' if w<106 else 'body')
    return b

def app(folder, title, subtitle, native, lua=False):
    a=E.Element('app',name=title,version='2.0.0',icon='images/icon.png')
    r=node(a,'resources')
    if lua:
        for mod in ['tolua','luaDS','luaGUI']:
            node(r,'module',src=f'../../modules/{mod}.klf')
        node(r,'script',type='text/lua',src='lua/main.lua')
    node(r,'module',src=f'modules/app_{folder}.klf')
    for name,size in [('body',16),('small',14),('tiny',12),('heading',24)]:
        node(r,'font',name=name,size=size,src='../../fonts/ttf/arial_lite.ttf',type='ttf')
    surface(r,'bg',640,480,BG)
    surface(r,'panel',592,320,PANEL)
    body=node(a,'body',width=640,height=480,background='bg',
              onload=f'export:{native}_Init()',onopen=f'export:{native}_Open()',
              onclose=f'export:{native}_Close()',onunload=f'export:{native}_Shutdown()')
    label(body,'heading',title,24,18,430,30,'heading')
    label(body,'subtitle',subtitle,24,48,580,20,'small',MUTED)
    return a,r,body

def save(a,folder):
    E.indent(a,space='  ')
    E.ElementTree(a).write(ROOT/f'applications/{folder}/app.xml',encoding='UTF-8',xml_declaration=True)

def layouts():
    a,r,b=app('settings','Settings','DreamShell NeXT  /  Make it yours','SettingsApp')
    button(b,r,'back-btn','Menu',500,20,116,32,'export:SettingsApp_Back()')
    for i,title in enumerate(['Display','Sound','Startup','Clock','System']):
        button(b,r,f'tab-{i}',title,24+i*120,76,112,32,'export:SettingsApp_Tab()')
    for i in range(7):
        button(b,r,f'row-{i}','',24,118+i*39,592,36,'export:SettingsApp_Change()')
    label(b,'help','A changes a value. X changes it back.',24,393,592,18,'small',MUTED)
    label(b,'save-status','No unsaved changes',24,415,592,18,'small',ACCENT)
    label(b,'controls','D-pad Select / Adjust   X Previous   Y Tab   B Menu',24,442,430,22,'tiny',MUTED)
    button(b,r,'save-btn','Save settings',464,438,152,32,'export:SettingsApp_Save()')
    node(b,'dialog',name='dialog',font='body',x=70,y=140,width=500,height=220,
         onconfirm='export:SettingsApp_Confirm()',oncancel='export:SettingsApp_Cancel()')
    save(a,'settings')

    a,r,b=app('gdplay','GD Play','DreamShell NeXT  /  Play an original disc','gdplay')
    button(b,r,'exit-btn','Menu',500,20,116,32,'export:gdplay_Back()')
    node(r,'image',name='disc-art',src='images/disc.svg.png')
    surface(r,'art-panel',228,276,PANEL)
    node(b,'panel',x=24,y=88,width=228,height=276,background='art-panel')
    node(b,'image',name='disc-image',src='disc-art',x=46,y=124,width=184,height=184)
    label(b,'disc-type','DISC DRIVE',44,96,190,24,'small',ACCENT)
    label(b,'disc-state','Checking disc...',44,326,192,26,'body',MUTED)
    label(b,'title1-txt','Insert a Dreamcast disc',276,92,340,28,'heading')
    label(b,'title2-txt','',276,122,340,28,'heading')
    label(b,'title3-txt','',276,152,340,22)
    for i,(key,title) in enumerate([('region','Region'),('vga','VGA support'),('date','Released'),('disk-num','Disc'),('version','Version'),('product','Product ID')]):
        label(b,key+'-label',title,276,192+i*28,132,24,'small',MUTED)
        label(b,key+'-txt','--',416,192+i*28,200,24)
    label(b,'status','Close the lid to read the disc.',24,376,592,40,'small',MUTED)
    button(b,r,'play-btn','Play disc',24,426,284,36,'export:gdplay_play()')
    button(b,r,'refresh-btn','Read disc again',320,426,296,36,'export:gdplay_Refresh()')
    save(a,'gdplay')

    a,r,b=app('filemanager','File Manager','DreamShell NeXT  /  Two panes. Clear destinations.','FileManagerApp',True)
    button(b,r,'exit-btn','Menu',500,20,116,32,'console:app -o')
    for name,color in [('white-bg','#14212E'),('blue-bg','#1B3041'),('row','#182838'),('row-focus','#225765'),('row-select','#155B71')]:
        surface(r,name,288 if name.endswith('bg') else 268,224 if name.endswith('bg') else 28,color)
    for i,side in enumerate(['top','bottom']):
        x=24+i*304
        button(b,r,f'path-{side}','/',x,80,288,32,f'FileManager:choosePane({i})')
        button(b,r,f'up-{side}','Up',x,118,64,28,f'FileManager:up({i})')
        button(b,r,f'device-{side}','Devices',x+72,118,104,28,f'FileManager:devices({i})')
        button(b,r,f'refresh-{side}','Refresh',x+184,118,104,28,f'FileManager:refresh({i})')
        node(b,'filemanager',name=f'filemgr-{side}',path='/',x=x,y=154,width=288,height=224,
             background='white-bg',item_font='small',item_font_color=TEXT,
             item_normal='row',item_highlight='row-focus',item_pressed='row-select',item_disabled='row',
             item_selected_normal='row-select',item_selected_highlight='row-focus',item_selected_pressed='row-select',item_selected_disabled='row',
             onclick=f'FileManagerItemClick{side.title()}',oncontextclick=f'FileManagerItemContextClick{side.title()}',
             onselect=f'FileManagerItemSelect{side.title()}')
    label(b,'title','Select a file or folder.',24,382,592,24,'small',ACCENT)
    p=node(b,'panel',name='toolbar-panel',x=24,y=410,width=592,height=28)
    for i,(name,text,fn) in enumerate([('copy','Copy','toolbarCopy'),('rename','Rename','toolbarRename'),('mkdir','New folder','toolbarMkdir'),('delete','Delete','toolbarDelete'),('archive','Archive','toolbarArchive'),('mount','Mount ISO','toolbarMountISO')]):
        button(p,r,name+'-btn',text,i*100,0,92,28,f'FileManager:{fn}()')
    label(b,'controls','D-pad Browse / Switch pane   A Open   B Up   X Copy   Y Actions',24,446,592,22,'tiny',MUTED)
    node(b,'dialog',name='modal-dialog',font='body',x=70,y=130,width=500,height=240,
         onconfirm='FileManager:ModalClick(true)',oncancel='FileManager:ModalClick(false)')
    save(a,'filemanager')

# A small vector vocabulary, legible in the launcher's 32px list and 128px preview.
ICONS={
 'launch_app': ('#62DDD0','<rect x="17" y="17" width="12" height="12" rx="2"/><rect x="35" y="17" width="12" height="12" rx="2"/><rect x="17" y="35" width="12" height="12" rx="2"/><rect x="35" y="35" width="12" height="12" rx="2"/>'),
 'main': ('#62DDD0','<path d="M15 30L32 16 49 30M20 28v20h24V28M28 48V35h8v13"/>'),
 'filemanager': ('#E8BD68','<path d="M14 23h15l5 5h16v21H14zM14 23v-5h15l5 5h16v5"/>'),
 'gdplay': ('#6FE3C0','<circle cx="30" cy="30" r="17"/><circle cx="30" cy="30" r="4"/><path d="M39 38l13 8-13 8z" fill="#14212E"/>'),
 'gd_ripper': ('#69D6EC','<circle cx="30" cy="27" r="16"/><circle cx="30" cy="27" r="4"/><path d="M39 34v18m-7-7l7 7 7-7M16 48v6h12"/>'),
 'iso_loader': ('#8CABFA','<path d="M18 16h22l7 7v25H18zM40 16v9h7"/><circle cx="30" cy="36" r="8"/><circle cx="30" cy="36" r="2"/>'),
 'games_menu': ('#BAA2EE','<path d="M22 23h20c4 0 7 4 8 10l3 10c1 7-6 9-10 3l-4-4H25l-4 4c-4 6-11 4-10-3l3-10c1-6 4-10 8-10zM23 29v10m-5-5h10"/><circle cx="40" cy="31" r="1.5"/><circle cx="46" cy="37" r="1.5"/>'),
 'settings': ('#91BACB','<path d="M15 21h34M15 32h34M15 43h34"/><rect x="23" y="16" width="6" height="10" rx="2" fill="#14212E"/><rect x="37" y="27" width="6" height="10" rx="2" fill="#14212E"/><rect x="20" y="38" width="6" height="10" rx="2" fill="#14212E"/>'),
 'vmu_manager': ('#80D4C2','<rect x="19" y="11" width="26" height="43" rx="5"/><rect x="24" y="17" width="16" height="15" rx="1"/><path d="M25 40v8m-4-4h8M35 40h4m-4 7h4"/>'),
 'bios_flasher': ('#EDA07A','<rect x="20" y="20" width="24" height="24" rx="3"/><path d="M24 12v8m8-8v8m8-8v8M24 44v8m8-8v8m8-8v8M12 24h8m-8 8h8m-8 8h8M44 24h8m-8 8h8m-8 8h8M34 24l-7 10h8l-5 7"/>'),
 'region_changer': ('#88CDE5','<circle cx="32" cy="32" r="19"/><ellipse cx="32" cy="32" rx="9" ry="19"/><path d="M13 32h38M17 22h30M17 42h30"/>'),
 'network': ('#6BBEEB','<rect x="25" y="13" width="14" height="12" rx="2"/><path d="M32 25v11M18 36h28M18 36v7m28-7v7"/><rect x="12" y="43" width="12" height="9" rx="2"/><rect x="40" y="43" width="12" height="9" rx="2"/>'),
 'dreameye': ('#E5A1C7','<rect x="13" y="22" width="38" height="28" rx="4"/><path d="M20 22l4-7h16l4 7"/><circle cx="32" cy="36" r="9"/>'),
 'speedtest': ('#D6D575','<path d="M15 46a22 22 0 1 1 34 0M32 36l13-15M15 33h5m12-18v5m17 13h-5"/><circle cx="32" cy="36" r="3"/>'),
 'memtest': ('#ABA9E5','<rect x="12" y="21" width="40" height="23" rx="2"/><path d="M17 44v7m8-7v7m8-7v7m8-7v7m7-7v7"/><rect x="18" y="27" width="10" height="10"/><rect x="36" y="27" width="10" height="10"/>'),
 'cart_ripper': ('#DAA272','<path d="M17 16h30v37H17zM23 16v14h18V16M24 46h16"/>'),
 'arcade': ('#E999AB','<path d="M18 11h28l-3 25 7 17H14l7-17zM23 17h18l-2 14H25zM22 41h20M27 37v4"/>'),
 'mie_jvs': ('#D5ACDF','<path d="M14 36h36v15H14zM25 36V24m-7 19h3m21 0h3"/><circle cx="25" cy="19" r="5"/>')
}

def icons():
    from cairosvg import svg2png
    for folder,(color,glyph) in ICONS.items():
        path=ROOT/f'applications/{folder}/images'
        path.mkdir(exist_ok=True)
        svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><rect x="1" y="1" width="62" height="62" rx="14" fill="#14212E" stroke="#30485C"/><g fill="none" stroke="{color}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">{glyph}</g></svg>\n'
        (path/'icon-next.svg').write_text(svg)
        tree=E.parse(ROOT/f'applications/{folder}/app.xml')
        target=ROOT/f'applications/{folder}'/tree.getroot().get('icon')
        svg2png(bytestring=svg.encode(), write_to=str(target), output_width=64, output_height=64)
    # An illustration from the same vector vocabulary; no legacy photographic skin.
    path=ROOT/'applications/gdplay/images'
    svg='<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"><circle cx="128" cy="128" r="108" fill="#233E50" stroke="#6FE3C0" stroke-width="3"/><circle cx="128" cy="128" r="88" fill="none" stroke="#345666" stroke-width="2"/><path d="M63 58A96 96 0 0 1 158 37M99 219A96 96 0 0 0 196 193" fill="none" stroke="#7FD8DB" stroke-width="9"/><circle cx="128" cy="128" r="28" fill="#101923" stroke="#9DB8C6" stroke-width="3"/><circle cx="128" cy="128" r="11" fill="#1B2A39"/></svg>'
    (path/'disc.svg').write_text(svg+'\n')
    svg2png(bytestring=svg.encode(), write_to=str(path/'disc.svg.png'))

if __name__=='__main__':
    layouts()
    icons()
