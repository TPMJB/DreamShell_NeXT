#!/usr/bin/env python3
"""Build NeXT's original vector splash and synthesized chime (host-only).

Requires Pillow, CairoSVG and ffmpeg. Normal builds use the committed assets.
The KMG layout follows KallistiOS utils/kmgenc; no encoder runs on the console.
"""
import array
import gzip
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]


def splash():
    import cairosvg
    from PIL import Image
    png = ROOT / 'resources/logo_sq.png'
    cairosvg.svg2png(url=str(ROOT / 'resources/boot-splash.svg'), write_to=str(png))
    preview = (ROOT / 'resources/boot-splash.svg').read_text().replace(
        'width="1024" height="512" viewBox="0 0 1024 512"',
        'width="640" height="480" viewBox="0 0 640 480"')
    cairosvg.svg2png(bytestring=preview.encode(), write_to=str(ROOT / 'resources/boot-preview.png'))
    im = Image.open(png).convert('RGB')
    width, height = im.size
    assert (width, height) == (1024, 512)
    # Rectangular twiddling: consecutive 512x512 Morton-order tiles.
    interleave = [sum(((n >> bit) & 1) << (2 * bit) for bit in range(10))
                  for n in range(1024)]
    pixels = array.array('H', [0]) * (width * height)
    for y in range(height):
        for x in range(width):
            r, g, b = im.getpixel((x, y))
            index = (interleave[y] | (interleave[x & 511] << 1)) + (x // 512) * 512**2
            pixels[index] = ((r * 31 // 255) << 11) | ((g * 63 // 255) << 5) | (b * 31 // 255)
    if sys.byteorder != 'little':
        pixels.byteswap()
    header = struct.pack('<7I36x', 0x00474d4b, 1, 1, 0x203, width, height, len(pixels) * 2)
    (ROOT / 'romdisk/logo.kmg.gz').write_bytes(gzip.compress(header + pixels.tobytes(), mtime=0))


def chime():
    # Three soft, rising tones with an exponential decay; no sampled audio.
    rate, frames = 44100, 196608
    samples = array.array('h')
    notes = [(0.08, 440.0, 0.22), (0.30, 554.365, 0.23), (0.52, 659.255, 0.26)]
    for n in range(frames):
        t = n / rate
        value = 0.0
        for start, frequency, level in notes:
            age = t - start
            if 0 <= age < 2.3:
                env = min(age / 0.025, 1.0) * math.exp(-2.5 * age)
                env *= min((2.3 - age) / 0.2, 1.0)
                value += level * env * (math.sin(2 * math.pi * frequency * age)
                                       + 0.12 * math.sin(4 * math.pi * frequency * age))
        sample = round(max(-1.0, min(1.0, value)) * 26000)
        samples.extend((sample, sample))
    if sys.byteorder != 'little':
        samples.byteswap()
    wav = ROOT / 'resources/sfx/startup.wav'
    with wave.open(str(wav), 'wb') as out:
        out.setparams((2, 2, rate, frames, 'NONE', 'not compressed'))
        out.writeframes(samples.tobytes())
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / 'startup.raw'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav),
                        '-ac', '2', '-ar', '44100', '-c:a', 'adpcm_yamaha',
                        '-f', 's16le', str(raw)], check=True)
        data = raw.read_bytes()
    assert len(data) == frames and len(data) % 32 == 0
    (ROOT / 'romdisk/startup.raw.gz').write_bytes(gzip.compress(data, mtime=0))


if __name__ == '__main__':
    splash()
    chime()
    print('Built NeXT splash (1 MiB texture) and 44.1 kHz stereo ADPCM chime.')
