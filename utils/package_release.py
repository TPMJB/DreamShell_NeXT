#!/usr/bin/env python3
"""Validate and seal a complete build. Publishing is a separate workflow."""
import hashlib
import json
from pathlib import Path
import re
import posixpath
import struct
import subprocess
import xml.etree.ElementTree as ET
import io
import wave
from generate_menu_music import TRACKS
from zipfile import ZipFile, ZIP_DEFLATED
from package_boot_branding import VERSION as BOOT_VERSION, verify_cdi
from build_provenance import validate_archive
from release_docs import GUIDES, render_guide

ROOT = Path(__file__).resolve().parents[1]


def main():
    version = (ROOT/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', version):
        raise ValueError('VERSION must be a numeric release version')
    output = ROOT/f'K-UI-v{version}.zip'
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    info = dict(project='K-UI', version=version, source_commit=commit,
                base_core='DreamShell 4.0.5 Beta 3', bootloader=BOOT_VERSION,
                build_kind='complete integration build',
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
        'README-FIRST.md', 'upstream-review.md', f'K-UI_bootloader_v{BOOT_VERSION}.cdi',
        f'K-UI-v{version}.cdi',
    }
    with ZipFile(ROOT/'DreamShell-dev.zip') as source:
        if source.testzip() is not None:
            raise ValueError('Full build archive failed ZIP CRC validation')
        if len(source.namelist()) != len(set(source.namelist())):
            raise ValueError('Full build archive contains duplicate paths')
        names = set(source.namelist())
        missing = required - names
        if missing:
            raise ValueError(f'Incomplete full release: {sorted(missing)}')
        info['provenance'] = validate_archive(source, ROOT, commit, version)
        verify_cdi(source.read(f'K-UI_bootloader_v{BOOT_VERSION}.cdi'))
        for path in names:
            if path.startswith('/') or '..' in Path(path).parts:
                raise ValueError(f'Unsafe archive path: {path}')
        # The new loader installs ELF payloads and removes the legacy BINs.
        firmware = sorted(n for n in names if n.startswith('DS/firmware/isoldr/') and n.endswith('.elf'))
        if not all(f'DS/firmware/isoldr/{device}.elf' in firmware for device in ('sd','ide','cd')):
            raise ValueError('SD/IDE/CD ISO Loader firmware is missing')
        for name in firmware:
            elf = source.read(name)
            if elf[:7] != b'\x7fELF\x01\x01\x01' or b'game loader v0.9.2\n' not in elf:
                raise ValueError(f'Stale or invalid loader: {name}')
            hdr = struct.unpack_from('<HHIIIIIHHHHHH',elf,16)
            entry,phoff,shoff = hdr[3:6]
            phsize,phnum,shsize,shnum = hdr[8:12]
            if hdr[1]!=42 or phsize!=32 or phoff+phnum*phsize>len(elf):
                raise ValueError(f'Invalid SH-4 loader headers: {name}')
            loads = [p for i in range(phnum) if (p:=struct.unpack_from('<IIIIIIII',elf,phoff+i*phsize))[0]==1]
            if not loads or any(p[4]>p[5] or p[1]+p[4]>len(elf) for p in loads):
                raise ValueError(f'Invalid loader segments: {name}')
            first,last = min(p[2] for p in loads),max(p[2]+p[5] for p in loads)
            if entry!=first or last-first+1024>2*1024*1024 or shsize!=40 or shoff+shsize*shnum>len(elf):
                raise ValueError(f'Loader does not fit its runtime area: {name}')
        info['iso_loader_firmware'] = '0.9.2'
        # Validate against the integrated source, not old release version
        # constants. A successful ZIP step must not hide stale app binaries/XML.
        versions = {}
        for app_xml in sorted((ROOT/'applications').glob('*/app.xml')):
            app = app_xml.parent.name
            path = f'DS/apps/{app}/app.xml'
            xml = source.read(path)
            if xml != app_xml.read_bytes():
                raise ValueError(f'Packaged {app} XML differs from source')
            metadata = ET.fromstring(xml)
            versions[app] = metadata.get('version')
            icon = metadata.get('icon')
            if icon and posixpath.normpath(f'DS/apps/{app}/{icon}') not in names:
                raise ValueError(f'Missing {app} launcher icon')
            for module in metadata.findall('resources/module'):
                module_path = posixpath.normpath(f'DS/apps/{app}/'+module.get('src'))
                data = source.read(module_path)
                if len(data)<1024 or data[:7]!=b'\x7fELF\x01\x01\x01' or int.from_bytes(data[18:20],'little')!=42:
                    raise ValueError(f'Invalid SH-4 module: {module_path}')
        info['app_versions'] = versions
        for track_name in TRACKS:
            data = source.read('DS/apps/launch_app/music/'+track_name)
            if len(data) > 2*1024*1024:
                raise ValueError(f'Music exceeds runtime memory limit: {track_name}')
            with wave.open(io.BytesIO(data)) as track:
                if (track.getnchannels(),track.getsampwidth(),track.getframerate()) != (1,2,22050):
                    raise ValueError(f'Invalid bundled music: {track_name}')
        info['music_tracks'] = list(TRACKS)
        for path in ('DS/apps/launch_app/music/menu.wav',
                     'DS/apps/vmu_manager/modules/app_vmu_manager.klf',
                     'DS/modules/isoldr.klf', 'DS/modules/isofs.klf'):
            if path not in names or len(source.read(path)) < 1024:
                raise ValueError(f'Missing integrated component: {path}')
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
                if name.endswith('/') or name in GUIDES:
                    continue
                data = source.read(name)
                dest.writestr(name, data)
                checksums.append(f'{hashlib.sha256(data).hexdigest()}  {name}\n')
            for name in GUIDES:
                data = render_guide(ROOT, name, commit)
                dest.writestr(name, data)
                checksums.append(f'{hashlib.sha256(data).hexdigest()}  {name}\n')
            # The bootloader guide refers to this optional configuration
            # template. Include it in the complete build as well as its ZIP.
            data = (ROOT/'resources/boot.cfg.example').read_bytes()
            dest.writestr('boot.cfg.example', data)
            checksums.append(f'{hashlib.sha256(data).hexdigest()}  boot.cfg.example\n')
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
