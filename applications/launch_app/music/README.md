# K-UI synth soundtrack

Five original instrumental loops, synthesized on the build host with no external
samples, recordings or sound banks:

| File | Track | Character | BPM |
| --- | --- | --- | --- |
| `menu.wav` | After Hours | Warm chords and FM bells | 80 |
| `neon-circuit.wav` | Neon Circuit | Pulsing bass and bright arpeggios | 100 |
| `orbital-drift.wav` | Orbital Drift | Spacious pads and sparse percussion | 80 |
| `midnight-vector.wav` | Midnight Vector | Faster electro rhythm | 110 |
| `chrome-horizon.wav` | Chrome Horizon | Warm major chords and synth lead | 90 |

Created for TPMJB's K-UI with Codex assistance. The composition and synthesizer
are in `utils/generate_menu_music.py`. Python 3's standard library reproduces all
five WAVs during the build; no music service or decoder is required.

A random track is selected when entering Launcher or GD Ripper. It loops until
you leave the app. Off/On keeps that same cached track. Consecutive visits avoid
repeats while the module remains loaded; a module reload can repeat a track.
A missing/invalid selected file falls back to `menu.wav`.

## Controls and performance

Press **Y**, keyboard **M**, or click **Y Music** to cycle
**15% → 30% → 50% → 75% → 100% → Off**. The master volume still applies.
`DS/apps/launch_app/music.cfg` stores the level. An asterisk means saving is
pending or failed. App exit stops playback and frees the buffers.

The five files total about 4.5 MiB on storage. Only one mono PCM16 WAV is cached:
at most 1,058,444 bytes for these tracks, plus the existing audio buffers. There
is no runtime synthesizer, additional decoder, crossfade or playlist streaming.
Playback has the same CPU/AICA work as the single-track player.

GD Ripper loads music only from SD, IDE/CF or PC, never from the disc. Its drive
operations block new music loads and preference writes; cached playback continues
from RAM. If there is no cached track, the control shows queued until the drive
operation finishes. Check throughput, sound and a known-good catalog CRC on the
console before the full release.

## Custom tracks

Any listed filename can be replaced with a RIFF WAV: **mono, 16-bit PCM,
8–44.1 kHz, at most 2 MiB**. For example:

```sh
ffmpeg -i your-track.ogg -t 24 -ar 22050 -ac 1 -c:a pcm_s16le menu.wav
```

Choose matching musical endpoints for a smooth loop. Keep backups: installing
an update overwrites all five supplied filenames. To hear only your own track,
replace `menu.wav` and remove the other four; missing selections fall back to it.

## Music permissions

The generated musical material and recordings may be used, modified and
redistributed with K-UI, including public downloads. To the extent rights exist
in this generated material, no additional restrictions or attribution
requirements are asserted. Existing source-code licenses remain unchanged.
