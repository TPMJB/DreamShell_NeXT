#!/usr/bin/env python3
"""Render 'After Hours', an original NeXT menu loop with no sampled material.

Uses only Python's standard library. All notes, instruments and echoes are defined here; no music
service, copyrighted recording or external sound bank is involved.
"""
from pathlib import Path
import argparse
from array import array
import math
import sys
import wave

RATE = 22050
BPM = 80
BEAT = 60 / BPM
SECONDS = 32 * BEAT


def compose():
    size = round(RATE * SECONDS)
    out = array('d', [0.0]) * size

    def add(start, note, seconds, gain, instrument):
        hz = 440 * 2 ** ((note - 69) / 12)
        first = round(start * RATE)
        for i in range(round(seconds * RATE)):
            t = i / RATE
            phase = 2 * math.pi * hz * t
            if instrument == 'pad':
                tone = (math.sin(phase) + .24 * math.sin(phase * 2 + .15)
                        + .1 * math.sin(phase * 3))
                envelope = min(t / .55, 1) * min((seconds - t) / 1.1, 1)
                tone *= .88 + .12 * math.sin(2 * math.pi * .22 * t)
            elif instrument == 'bell':
                tone = math.sin(phase + 1.1 * math.exp(-t * 3) * math.sin(2 * phase))
                envelope = (1 - math.exp(-t * 90)) * math.exp(-t * 1.35)
                envelope *= min((seconds - t) / .15, 1)
            else:
                tone = math.sin(phase) + .12 * math.sin(phase * 2)
                envelope = min(t / .08, 1) * min((seconds - t) / .3, 1)
            # Wrap releases into the beginning: the loop keeps its ambience.
            out[(first + i) % size] += gain * tone * max(0,min(envelope,1))

    # Dmaj9 -> Bm9 -> Gmaj9 -> A6/9, two bars per chord.
    chords = [(38,[62,66,69,73,76]), (35,[62,66,69,73,74]),
              (31,[59,62,66,69,74]), (33,[61,64,66,69,71])]
    for c, (root, notes) in enumerate(chords):
        at = c * 8 * BEAT
        for j, note in enumerate(notes):
            add(at + j * .018, note, 8 * BEAT + .85, .041, 'pad')
        for beat in (0,4):
            add(at + beat * BEAT, root, 3.6 * BEAT, .055, 'bass')
    melody = [(0.5,78), (2.5,76), (5,73), (7,69),
              (9,74), (11.5,73), (14,69),
              (16.5,71), (19,74), (21.5,78),
              (24.5,76), (27,73), (29,71), (31,69)]
    for beat, note in melody:
        add(beat * BEAT, note, 2.7, .052, 'bell')
    dry = array('d', out)
    for delay, gain in ((.375,.18),(.75,.11),(1.125,.055),(1.77,.035)):
        shift = round(delay * RATE)
        for i in range(size):
            out[i] += gain * dry[(i-shift) % size]
    mean = sum(out) / size
    factor = .44 / max(abs(value-mean) for value in out)
    pcm = array('h', (round((value-mean)*factor*32767) for value in out))
    if sys.byteorder != 'little':
        pcm.byteswap()
    return pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] /
                        'applications/launch_app/music/menu.wav')
    args = parser.parse_args()
    pcm = compose()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.output), 'wb') as file:
        file.setparams((1,2,RATE,0,'NONE','not compressed'))
        file.writeframes(pcm.tobytes())
    print(f'{args.output}: {len(pcm)/RATE:.1f}s, mono PCM16, {RATE}Hz, '
          f'{args.output.stat().st_size:,} bytes')


if __name__ == '__main__':
    main()
