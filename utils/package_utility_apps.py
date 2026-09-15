#!/usr/bin/env python3
"""Extract an app-only update; preserve separately developed core/loader fixes."""
from zipfile import ZipFile, ZIP_DEFLATED
from pathlib import Path
import sys
import xml.etree.ElementTree as E
ROOT=Path(__file__).resolve().parents[1]
APPS = {'filemanager':'2.0.1', 'settings':'2.0.2', 'gdplay':'2.0.2',
        'bios_flasher':'3.0.1', 'region_changer':'2.0.1', 'speedtest':'2.0.1',
        'memtest':'2.0.1', 'network':'2.0.1'}

def package(source,dest):
    with ZipFile(source) as full, ZipFile(dest,'w',ZIP_DEFLATED) as out:
        assert full.testzip() is None
        files={'DS/fonts/ttf/arial_lite.ttf'}
        prefixes=tuple('DS/apps/'+app+'/' for app in APPS)
        for app,version in APPS.items():
            xml=E.fromstring(full.read('DS/apps/'+app+'/app.xml'))
            assert xml.get('version')==version
            assert len(full.read('DS/apps/'+app+'/modules/app_'+app+'.klf'))>1024
        for path in (ROOT/'applications').glob('*/app.xml'):
            icon=E.parse(path).getroot().get('icon')
            if (path.parent/'images/icon-next.svg').exists():
                files.add('DS/apps/'+path.parent.name+'/'+icon)
        for name in sorted(full.namelist()):
            if name.startswith(prefixes) or name in files:
                out.writestr(name,full.read(name))
        out.write(ROOT/'utils/README.utility-apps.md','Utility-Apps-README.md')
        assert files.issubset(out.namelist())
        assert 'DS/DS_CORE.BIN' not in out.namelist()

if __name__=='__main__': package(*sys.argv[1:])
