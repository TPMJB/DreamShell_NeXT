#!/usr/bin/env python3
"""Render production menu geometry captured by the bootloader host harness.

The host substitutes Courier for the console BIOS font. These previews check
layout and wording; they do not simulate PVR/VRAM or prove console rendering.
"""
import csv
import os
from pathlib import Path
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, BOOT_PREVIEW_DIR=str(output))
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s',
                    'utils/tests', '-p', 'test_bootloader.py'],
                   cwd=ROOT, env=env, check=True)
    font = ImageFont.truetype(str(ROOT/'resources/fonts/ttf/cour.ttf'), 20)
    for name in ('no-storage', 'storage-found', 'read-error'):
        canvas = Image.new('RGB', (640, 480), (9, 19, 33))
        draw = ImageDraw.Draw(canvas)
        with (output/f'{name}.csv').open() as stream:
            for kind, character, x, y, width, height, color in csv.reader(stream):
                x, y, width, height = map(lambda n: round(float(n)), (x, y, width, height))
                if width <= 0 or height <= 0:
                    continue
                rgb = tuple(bytes.fromhex(color)[1:])
                if kind == 'B':
                    draw.rectangle((x, y, x+width-1, y+height-1), fill=rgb)
                else:
                    glyph = Image.new('RGBA', (12, 24))
                    ImageDraw.Draw(glyph).text((0, 0), chr(int(character)), font=font, fill=(*rgb, 255))
                    glyph = glyph.resize((width, height), getattr(Image, 'Resampling', Image).BILINEAR)
                    canvas.paste(glyph, (x, y), glyph)
        canvas.save(output/f'{name}.png')
        (output/f'{name}.csv').unlink()
    print(f'Production layout previews (host font substitute): {output}')


if __name__ == '__main__':
    main()
