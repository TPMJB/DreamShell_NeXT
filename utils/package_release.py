#!/usr/bin/env python3
"""Validate and seal the full build before a versioned GitHub release."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]


def main():
    version = (ROOT/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('VERSION must be a numeric release version')
    output = ROOT/f'DreamShell-NeXT-v{version}.zip'
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    info = dict(project='DreamShell NeXT', version=version, source_commit=commit,
                base_core='DreamShell 4.0.5 Beta 3', bootloader='3.0',
                gd_ripper='2.2.0', launcher='2.0.2', iso_loader='0.9.0',
                kallistios=(ROOT/'sdk/doc/KallistiOS.txt').read_text().strip(),
                kernel_patches=sorted(p.name for p in (ROOT/'sdk/kos-patches').glob('*.patch')))
    required = {
        'DS/DS_CORE.BIN', 'DS/DEBUG_DS_CORE.BIN', 'DS/EMU_DS_CORE.BIN',
        'DS/modules/vkb.klf', 'DS/fonts/ttf/arial_lite.ttf', 'DS/NEXT_VERSION',
        'DS/apps/gd_ripper/app.xml', 'DS/apps/gd_ripper/redump.db',
        'DS/apps/gd_ripper/modules/app_gd_ripper.klf',
        'DS/apps/launch_app/app.xml', 'DS/apps/launch_app/modules/app_launch_app.klf',
        'DS/doc/LICENSE', 'DS/doc/NOTICE', 'DS/lua/startup.lua',
        'host-tools/verify_gd_dump.py', 'host-tools/make_gd_redump_db.py',
        'exfat-guide.md', 'input-ui-guide.md', 'readback-guide.md',
        'README-FIRST.md', 'upstream-review.md', 'DreamShell_bootloader_v3.0.cdi',
        f'DreamShell-NeXT-v{version}.cdi',
    }
    with ZipFile(ROOT/'DreamShell-dev.zip') as source:
        if source.testzip() is not None:
            raise ValueError('Full build archive failed ZIP CRC validation')
        names = set(source.namelist())
        missing = required - names
        if missing:
            raise ValueError(f'Incomplete full release: {sorted(missing)}')
        for path in names:
            if path.startswith('/') or '..' in Path(path).parts:
                raise ValueError(f'Unsafe archive path: {path}')
        if not any(n.startswith('DS/firmware/isoldr/') and n.endswith('.bin') for n in names):
            raise ValueError('ISO Loader firmware is missing')
        for app, ver in [('gd_ripper', '2.2.0'), ('launch_app', '2.0.2')]:
            metadata = ET.fromstring(source.read(f'DS/apps/{app}/app.xml'))
            if metadata.get('version') != ver:
                raise ValueError(f'Wrong {app} version')
        expected_apps = {f'DS/apps/{p.parent.name}/app.xml' for p in (ROOT/'applications').glob('*/app.xml')}
        if not expected_apps <= names:
            raise ValueError(f'Missing standard apps: {sorted(expected_apps-names)}')
        info['packaged_apps'] = len(expected_apps)
        if source.read('DS/NEXT_VERSION').decode().strip() != version:
            raise ValueError('Packaged version differs from source')
        for variant in ['DS_CORE.BIN', 'DEBUG_DS_CORE.BIN', 'EMU_DS_CORE.BIN']:
            core = source.read('DS/'+variant)
            for asset in ['logo.kmg.gz', 'startup.raw.gz']:
                if (ROOT/'romdisk'/asset).read_bytes() not in core:
                    raise ValueError(f'{variant} is missing the new embedded {asset}')
        checksums = []
        with ZipFile(output, 'w', ZIP_DEFLATED, compresslevel=9) as dest:
            for name in sorted(names):
                if name.endswith('/'):
                    continue
                data = source.read(name)
                dest.writestr(name, data)
                checksums.append(f'{hashlib.sha256(data).hexdigest()}  {name}\n')
            metadata = (json.dumps(info, indent=2)+'\n').encode()
            dest.writestr('build-info.json', metadata)
            checksums.append(f'{hashlib.sha256(metadata).hexdigest()}  build-info.json\n')
            dest.writestr('SHA256SUMS', ''.join(checksums))
    # Ubuntu 22.04's Python 3.10 predates hashlib.file_digest.
    checksum = hashlib.sha256()
    with output.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            checksum.update(block)
    digest = checksum.hexdigest()
    (ROOT/'SHA256SUMS').write_text(f'{digest}  {output.name}\n')
    print(json.dumps(info, indent=2))
    print(f'{output.name}: {output.stat().st_size} bytes, SHA-256 {digest}')


if __name__ == '__main__':
    main()
