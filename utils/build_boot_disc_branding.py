#!/usr/bin/env python3
"""Render the boot-disc badge and inject it into the existing bootstrap.

Requires CairoSVG, Pillow and KallistiOS makeip on the host. Normal builds
use the committed IP.BIN. The bootstrap code and disc header are preserved.
MR format: https://github.com/KallistiOS/KallistiOS/tree/master/utils/makeip
"""
import argparse
import io
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MR_OFFSET = 0x3820
MR_LIMIT = 8192


def validate_badge(data):
    if len(data) < 30 or data[:2] != b'MR':
        raise ValueError('Missing MR header')
    size, _, offset, width, height, _, colors = struct.unpack_from('<7I', data, 2)
    if not (30 <= size == len(data) <= MR_LIMIT):
        raise ValueError('Badge exceeds the bootstrap logo space')
    if not (0 < width <= 320 and 0 < height <= 90 and 0 < colors <= 128):
        raise ValueError('Unsupported MR dimensions or palette')
    if offset != 30 + 4 * colors or offset >= size:
        raise ValueError('Invalid MR palette offset')


def inject_badge(bootstrap, badge):
    validate_badge(badge)
    if len(bootstrap) != 32768 or bootstrap[:16] != b'SEGA SEGAKATANA ':
        raise ValueError('Expected the existing 32 KiB Dreamcast bootstrap')
    if bootstrap[MR_OFFSET:MR_OFFSET+2] != b'MR':
        raise ValueError('Original bootstrap logo is missing')
    old_size = struct.unpack_from('<I', bootstrap, MR_OFFSET+2)[0]
    if not 30 <= old_size <= MR_LIMIT:
        raise ValueError('Original logo size is outside the reserved area')
    end = MR_OFFSET + max(old_size, len(badge))
    result = bytearray(bootstrap)
    result[MR_OFFSET:end] = badge.ljust(end-MR_OFFSET, b'\0')
    assert len(result) == len(bootstrap)
    assert result[:MR_OFFSET] == bootstrap[:MR_OFFSET]
    assert result[end:] == bootstrap[end:]
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--makeip', required=True, help='KallistiOS makeip executable')
    args = parser.parse_args()
    import cairosvg
    from PIL import Image

    art = ROOT / 'resources/boot-disc-badge.svg'
    rendered = cairosvg.svg2png(url=str(art))
    image = Image.open(io.BytesIO(rendered)).convert('RGB')
    assert image.size == (320, 90)
    # A compact palette and no dithering keep the MR within its 8 KiB budget.
    indexed = image.quantize(colors=64, method=Image.Quantize.MEDIANCUT,
                             dither=Image.Dither.NONE)
    palette = indexed.getpalette()
    transparent = next(i for i in range(64) if palette[3*i:3*i+3] == [192, 192, 192])
    with tempfile.TemporaryDirectory() as temp:
        png = Path(temp) / 'badge.png'
        mr = Path(temp) / 'badge.mr'
        indexed.save(png)
        subprocess.run([args.makeip, '-l', str(png), '-s', str(mr)], check=True)
        badge = mr.read_bytes()
    validate_badge(badge)
    target = ROOT / 'resources/IP.BIN'
    updated = inject_badge(target.read_bytes(), badge)
    (ROOT / 'resources/boot-disc-badge.mr').write_bytes(badge)
    indexed.save(ROOT / 'resources/boot-disc-badge.png', transparency=transparent)
    target.write_bytes(updated)
    print(f'Boot-disc badge: 320 x 90, {len(badge)} / {MR_LIMIT} bytes; bootstrap code preserved.')


if __name__ == '__main__':
    main()
