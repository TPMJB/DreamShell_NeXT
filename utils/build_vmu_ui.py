"""Generate the native 640x480 VMU Manager layout (no external artwork required)."""
from pathlib import Path
import xml.etree.ElementTree as E

ROOT = Path(__file__).resolve().parents[1]

def build():
    app = E.Element('app', name='VMU Manager', version='2.1.0', icon='images/icon.png',
                    extensions='.vmd .vmu .dci .vms .vmi')
    res = E.SubElement(app, 'resources')
    E.SubElement(res, 'module', src='modules/app_vmu_manager.klf')
    for name, size in [('body',16),('small',14),('mini',12),('title',24)]:
        E.SubElement(res, 'font', src='../../fonts/ttf/arial_lite.ttf', type='ttf', size=str(size), name=name)
    def surface(name,w,h,color,border=None):
        s=E.SubElement(res,'surface',name=name,width=str(w),height=str(h))
        E.SubElement(s,'fill',color=border or color)
        if border: E.SubElement(s,'fill',x='2',y='2',width=str(w-4),height=str(h-4),color=color)
        return name
    surface('bg',640,480,'#101923')
    surface('panel',592,68,'#192A39')
    surface('confirm-bg',568,226,'#182C3D','#53E1E3')
    surface('progress-bg',568,94,'#182C3D','#53E1E3')
    surface('progressbar_back',528,12,'#263749')
    surface('progressbar',528,12,'#53E1E3')
    surface('logo',48,48,'#192A39')
    surface('confirmimg',1,1,'#182C3D')
    surface('confirmimg0',1,1,'#182C3D')
    E.SubElement(res,'image',src='images/dump_icon.png',name='dump_icon')
    for suffix in ['', '2']:
        surface('item-normal'+suffix,264,26,'#192A39')
        surface('item-focus'+suffix,264,26,'#244A5D','#53E1E3')
        surface('item-selected'+suffix,264,26,'#1B5266','#53E1E3')
    surface('input-normal',480,36,'#223548')
    surface('input-focus',480,36,'#155B71','#53E1E3')
    sizes=set()
    def button(parent,name,text,x,y,w=140,h=32,action='VMU_Manager_Action',danger=False):
        key=f'b{w}-{h}'+('-danger' if danger else '')
        if key not in sizes:
            sizes.add(key)
            surface(key+'-normal',w,h,'#493039' if danger else '#223548')
            surface(key+'-highlight',w,h,'#694048' if danger else '#155B71','#FF9A99' if danger else '#53E1E3')
            surface(key+'-pressed',w,h,'#267F8A','#ACFFFF')
            surface(key+'-disabled',w,h,'#1B2631')
        b=E.SubElement(parent,'input',type='button',name=name,x=str(x),y=str(y),width=str(w),height=str(h),
                       normal=key+'-normal',highlight=key+'-highlight',pressed=key+'-pressed',disabled=key+'-disabled',
                       onclick='export:'+action+'()')
        E.SubElement(b,'label',font='body' if h>=32 else 'small',color='#EDF5FC',text=text,align='center',valign='center')
        return b
    def label(parent,name,text,x,y,w,h=20,font='body',color='#E7EEF4'):
        return E.SubElement(parent,'label',name=name,text=text,x=str(x),y=str(y),width=str(w),height=str(h),font=font,color=color)
    def panel(parent,name,x,y,w,h,background=None):
        attrs=dict(name=name,x=str(x),y=str(y),width=str(w),height=str(h))
        if background:attrs['background']=background
        return E.SubElement(parent,'panel',attrs)
    body=E.SubElement(app,'body',width='640',height='480',background='bg',onload='export:Vmu_Manager_Init()',
                      onopen='export:VMU_Manager_Open()',onclose='export:VMU_Manager_Close()',onunload='export:VMU_Manager_Shutdown()')
    label(body,'title','VMU Manager',24,14,420,30,'title')
    label(body,'version','NeXT / 2.1',514,20,104,20,'small','#53E1E3')
    label(body,'subtitle','Choose a memory card to manage its saves.',24,49,592,20,'body','#AFC2D4')
    pages=E.SubElement(body,'cardstack',name='pages',x='0',y='76',width='640',height='338')
    home=panel(pages,'main_page',0,0,640,338)
    label(home,'drection','CHOOSE A VMU',24,0,592,20,'small','#53E1E3')
    slots=panel(home,'vmu-container',24,36,592,162)
    for port in range(4):
        x=port*150
        label(home,'port-'+str(port),'PORT '+chr(65+port),24+x,24,140,20,'small','#AFC2D4')
        for slot in range(2):
            name=chr(65+port)+str(slot+1)
            button(slots,name,name+' / Empty',x,20+slot*76,140,64)
    label(home,'name-device','No VMU selected',24,222,592,24,'body')
    label(home,'free-mem','Connect a VMU to any controller port.',24,248,592,22,'body','#AFC2D4')
    label(home,'home-help','Copy saves between VMUs, SD, IDE and PC.',24,296,592,20,'small','#AFC2D4')
    manage=panel(pages,'vmu_page',0,0,640,338)
    label(manage,'left-title','VMU',24,0,264,20)
    label(manage,'left-path','',24,21,264,18,'small','#AFC2D4')
    label(manage,'right-title','OTHER LOCATION',352,0,264,20)
    label(manage,'right-path','Choose where to copy saves.',352,21,264,18,'small','#AFC2D4')
    for name,x,suffix in [('file_browser',24,''),('file_browser2',352,'2')]:
        # SDL_gui reserves 20 pixels even with its scrollbar removed. The
        # visible list panel and row surfaces remain 264 pixels wide.
        E.SubElement(manage,'filemanager',path='/',name=name,x=str(x),y='44',width='284',height='182',
                     item_normal='item-normal'+suffix,item_highlight='item-focus'+suffix,item_pressed='item-focus'+suffix,
                     item_disabled='item-normal'+suffix,item_font='small',item_font_color='#EDF5FC',
                     onclick='export:VMU_Manager_BrowseClick()',onselect='export:VMU_Manager_BrowseSelect()')
    for name,text,x,y in [('/sd','SD card',352,48),('/ide','IDE drive',492,48),('/pc','PC link',352,104),('/cd','Disc',492,104),('dst-vmu','Another VMU',352,160)]:
        button(manage,name,text,x,y,124,42)
    label(manage,'location-help','Select a location to browse.',352,210,264,18,'small','#AFC2D4')
    for name,text,x in [('copy-button','Copy save',24),('copy-all-button','Copy all >',174),('location-button','Location',324),('tools-button','More actions',474)]:
        button(manage,name,text,x,234,140,32)
    detail=panel(manage,'save-detail',24,274,592,64,'panel')
    E.SubElement(detail,'image',name='vmu-icon',src='logo',x='8',y='8',width='48',height='48')
    label(detail,'save-name','Select a save to see its details.',64,2,520,20,'body')
    label(detail,'save-size','',64,24,182,18,'small','#53E1E3')
    label(detail,'desc-short','',250,24,332,18,'small')
    label(detail,'desc-long','',64,43,520,18,'small','#AFC2D4')
    progress=panel(manage,'progressbar_container',36,88,568,94,'progress-bg')
    label(progress,'progress-label','Working... keep the VMU connected.',20,16,528,24)
    E.SubElement(progress,'progressbar',name='progressbar',x='20',y='58',width='528',height='12',pos='0',bimage='progressbar_back',pimage='progressbar')
    confirm=panel(manage,'confirm',36,52,568,226,'confirm-bg')
    label(confirm,'confirm-title','Confirm action',20,14,528,26,'title')
    for i in range(3):label(confirm,'confirm-text' if not i else 'confirm-line-'+str(i),'',20,54+i*24,528,24,'body')
    E.SubElement(confirm,'image',name='image-confirm',src='confirmimg',x='0',y='0',width='1',height='1')
    button(confirm,'modal-cancel','B  Cancel',20,160,164,42,'VMU_Manager_Confirm')
    button(confirm,'modal-accept','A  Continue',200,160,164,42,'VMU_Manager_Confirm',True)
    button(confirm,'modal-browse','X  Browse image',380,160,168,42,'VMU_Manager_Confirm')
    folder=panel(pages,'folder_page',0,0,640,338)
    label(folder,'folder-title','New folder',80,38,480,32,'title')
    label(folder,'folder-help','A name for this folder. Press A to open the keyboard.',80,82,480,22,'small','#AFC2D4')
    E.SubElement(folder,'input',type='text',name='folder-name',font='body',fontcolor='#EDF5FC',value='New folder',
                 x='80',y='122',width='480',height='36',normal='input-normal',highlight='input-focus',focus='input-focus')
    button(folder,'confirm-yes','Create folder',80,182,232,42)
    button(folder,'confirm-no','Cancel',328,182,232,42)
    tools=panel(pages,'tools_page',0,0,640,338)
    label(tools,'tools-title','More actions',24,0,592,26,'title')
    for name,text,y,danger in [('dump-button','Create full VMU image (.vmd)',40,False),('delete-button','Delete selected save / folder',88,True),('new-folder','Create a folder in the other location',136,False),('format-c','Format the selected VMU',184,True),('tools-back','Back to saves',244,False)]:
        button(tools,name,text,24,y,592,42,danger=danger)
    label(tools,'tools-help','Delete and Format ask for confirmation before changing data.',24,292,592,20,'small','#AFC2D4')
    label(body,'status','Ready.',24,420,592,20,'small','#AFC2D4')
    label(body,'controls','D-pad: move   A: select   B: back   X: copy   Y: actions',24,448,436,18,'mini','#AFC2D4')
    button(body,'home_but','VMUs',464,444,72,28)
    button(body,'exit-button','Exit',544,444,72,28)
    E.indent(app,'  ')
    E.ElementTree(app).write(ROOT/'applications/vmu_manager/app.xml',encoding='UTF-8',xml_declaration=True)

if __name__=='__main__':build()
