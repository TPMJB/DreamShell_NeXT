#!/usr/bin/env python3
"""Package the launcher overlay directly for GitHub's artifact ZIP wrapper."""
from pathlib import Path
import hashlib
import os
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def check_imports(module, kos, nm):
    allowed = set()
    for file in [ROOT/'exports.txt', ROOT/'exports_gcc.txt',
                 kos/'kernel/exports.txt',kos/'kernel/arch/dreamcast/exports.txt']:
        for line in file.read_text().splitlines():
            line = line.split('#',1)[0].strip()
            if line and not line.startswith('include '):
                allowed.add('_'+line.split()[0])
    imports = subprocess.check_output([nm,'-u',str(module)],text=True)
    names = {line.split()[-1] for line in imports.splitlines() if line.split()}
    unknown = names - allowed
    if unknown:
        raise SystemExit('Unavailable runtime imports: '+', '.join(sorted(unknown)))
    print(f'All {len(names)} KLF imports are exported by the pinned KOS/DreamShell core.')


def package(dest):
    app = ROOT/'applications/launch_app'
    module = app/'modules/app_launch_app.klf'
    elf = module.read_bytes()
    assert len(elf)>1024 and elf[:7]==b'\x7fELF\x01\x01\x01'
    assert struct.unpack_from('<HH',elf,16)==(1,42), 'Expected SH-4 relocatable ELF'
    files = [app/'app.xml',app/'catalog.xml',module,app/'music/README.md']
    files += sorted((app/'images').glob('*.png'))
    from generate_menu_music import TRACKS
    files += [app/'music'/name for name in TRACKS]
    for file in files:
        target = dest/'DS/apps/launch_app'/file.relative_to(app)
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(file,target)
    shutil.copyfile(ROOT/'utils/README.menu-music.md',dest/'Menu-Music-Guide.md')
    (dest/'SOURCE-REVISION.txt').write_text(os.getenv('SOURCE_SHA','local')+'\n')
    sums = []
    for file in sorted((dest/'DS').rglob('*')):
        if file.is_file():
            sums.append(hashlib.sha256(file.read_bytes()).hexdigest()+'  '+file.relative_to(dest).as_posix())
    (dest/'SHA256SUMS.txt').write_text('\n'.join(sums)+'\n')
    assert not list(dest.rglob('music.cfg')), 'Do not overwrite saved preferences'
    print(f'Packaged {len(sums)} launcher files in {dest}')


if __name__=='__main__':
    if sys.argv[1]=='check-imports':
        for app in ('launch_app', 'gd_ripper'):
            check_imports(ROOT/f'applications/{app}/modules/app_{app}.klf',Path(sys.argv[2]),sys.argv[3])
    else:
        package(Path(sys.argv[1]))
