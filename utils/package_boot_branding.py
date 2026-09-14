#!/usr/bin/env python3
"""Verify the generated boot CDI and package the TPMJB artwork edition."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

from build_boot_disc_branding import MR_OFFSET, validate_badge

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'DreamShell-NeXT-TPMJB-Bootdisc.zip'
CDI = 'DreamShell-NeXT-TPMJB-bootloader-v3.0.cdi'


def verify_cdi(data):
    # cdi4dc writes Mode 2 sectors with 2336 bytes per sector. Locate the
    # first user-data byte, then strip each sector's subheader and EDC/ECC.
    start = data.find(b'SEGA SEGAKATANA SEGA ENTERPRISES')
    if start < 0:
        raise ValueError('CDI bootstrap not found')
    if data[start+16*2336:start+16*2336+7] != b'\x01CD001\x01':
        raise ValueError('CDI ISO9660 data track is invalid')
    bootstrap = b''.join(data[start+i*2336:start+i*2336+2048] for i in range(16))
    with tempfile.TemporaryDirectory() as temp:
        expected = Path(temp)/'IP.BIN'
        subprocess.run(['bash', str(ROOT/'utils/update_ip_bin.sh'),
                        str(ROOT/'resources/IP.BIN'), str(expected),
                        '3', '0', '0', '0x30', 'bootloader'],
                       check=True, stdout=subprocess.DEVNULL)
        ref = expected.read_bytes()
    # Release date may cross midnight during a build; all other bytes must
    # match the normal updater's output exactly, including bootstrap code.
    if len(bootstrap) != 32768 or bootstrap[:80]+bootstrap[96:] != ref[:80]+ref[96:]:
        raise ValueError('CDI bootstrap differs from the branded source')
    badge = (ROOT/'resources/boot-disc-badge.mr').read_bytes()
    validate_badge(badge)
    if bootstrap[MR_OFFSET:MR_OFFSET+len(badge)] != badge:
        raise ValueError('CDI is missing the new boot badge')
    return start


def main():
    with ZipFile(ROOT/'DreamShell-dev.zip') as full:
        if full.testzip() is not None:
            raise ValueError('Build archive failed CRC validation')
        cdi = full.read('DreamShell_bootloader_v3.0.cdi')
    start = verify_cdi(cdi)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    info = dict(project='DreamShell NeXT', edition='TPMJB boot-disc branding',
                bootloader='3.0', source_commit=commit,
                badge='resources/boot-disc-badge.svg',
                website='https://github.com/TPMJB', console_tested=False)
    files = {CDI: cdi, 'build-info.json': (json.dumps(info, indent=2)+'\n').encode()}
    for src, dest in [('utils/README.boot-branding.md', 'README-FIRST.md'),
                      ('resources/boot-disc-badge.png', 'boot-disc-badge.png'),
                      ('LICENSE', 'LICENSE'), ('NOTICE', 'NOTICE')]:
        files[dest] = (ROOT/src).read_bytes()
    files['SHA256SUMS'] = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n'
                                for name, data in files.items()).encode()
    with ZipFile(ROOT/PACKAGE, 'w', ZIP_DEFLATED, compresslevel=9) as out:
        for name, data in files.items():
            out.writestr(name, data)
    print(f'Verified CDI bootstrap at byte {start}; wrote {PACKAGE}.')
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
