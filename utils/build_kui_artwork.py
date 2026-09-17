#!/usr/bin/env python3
"""Convert approved K-UI art to the existing Dreamcast texture/badge formats.

Requires Pillow, CairoSVG and KOS makeip/kmgenc. Source illustrations are kept
in resources/branding; this script only sizes and encodes them for hardware.
"""
import argparse
import base64
import gzip
import hashlib
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    import cairosvg
    from PIL import Image, ImageOps
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--makeip', required=True)
    parser.add_argument('--kmgenc', required=True)
    args = parser.parse_args()
    resample = getattr(Image, 'Resampling', Image).LANCZOS
    svg = (ROOT/'resources/boot-splash.svg').read_bytes().replace(
        b'branding/k-ui-splash.png', b'data:image/png;base64,' +
        base64.b64encode((ROOT/'resources/branding/k-ui-splash.png').read_bytes()))
    cairosvg.svg2png(bytestring=svg,
                    write_to=str(ROOT/'resources/logo_sq.png'))
    subprocess.run([args.kmgenc, '-v', str(ROOT/'resources/logo_sq.png')], check=True)
    raw = ROOT/'resources/logo_sq.kmg'
    (ROOT/'romdisk/logo.kmg.gz').write_bytes(gzip.compress(raw.read_bytes(), compresslevel=9, mtime=0))
    raw.unlink()
    with Image.open(ROOT/'resources/logo_sq.png') as source:
        source.crop((0,0,640,480)).save(ROOT/'resources/boot-preview.png')
    subprocess.run(['python3',str(ROOT/'utils/build_boot_disc_branding.py'),
                    '--makeip',args.makeip],check=True)
    with Image.open(ROOT/'resources/branding/k-ui-badge.png') as source:
        badge = ImageOps.contain(source.convert('RGB'),(256,128),resample)
        texture = Image.new('RGB',(256,128),'#080F23')
        texture.paste(badge,((256-badge.width)//2,(128-badge.height)//2))
        texture.save(ROOT/'applications/launch_app/images/brand.png')
        for app in ('main','iso_loader','bios_flasher','region_changer'):
            texture.save(ROOT/f'applications/{app}/images/logo.png')
    for vector in (ROOT/'applications').glob('*/images/icon-next.svg'):
        app = vector.parent.parent
        icon = ET.parse(app/'app.xml').getroot().get('icon')
        cairosvg.svg2png(url=str(vector), write_to=str(app/icon))
    (ROOT/'resources/next-branding.sha256').write_text(''.join(
        f'{hashlib.sha256((ROOT/name).read_bytes()).hexdigest()}  {name}\n'
        for name in ('resources/boot-disc-badge.mr','romdisk/logo.kmg.gz')))


if __name__ == '__main__':
    main()
