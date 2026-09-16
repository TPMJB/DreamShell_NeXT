# After Hours — DreamShell NeXT menu theme

An original 24-second instrumental loop created for TPMJB's DreamShell NeXT:
warm synthesizer chords, a soft FM bell melody and quiet bass at 80 BPM.
Generated algorithmically with Codex assistance, without external samples,
recordings, sound banks or borrowed melodies. This is not Sega game audio.

The editable composition and synthesizer are in `utils/generate_menu_music.py`.
Run it with Python 3 to reproduce `menu.wav`. The build generates the WAV from
this source; no large binary asset or additional Python package is required.

The generated recording and musical material may be used, modified and
redistributed with DreamShell NeXT, including in public downloads. To the extent
any rights exist in this generated material, no additional restrictions or
attribution requirements are asserted. Existing source-code licenses remain
unchanged.

## Controls

In Launch App or GD Ripper, press **Y** (keyboard **M**) or click **Y Music** to cycle
**15% → 30% → 50% → 75% → 100% → Off → 15%**. The level is relative to DreamShell's master
volume. A new installation starts at 15%. Your preference is stored in
`DS/apps/launch_app/music.cfg`. An asterisk means the preference is pending or could not be
saved; the current session still uses your selection.

Each app owns its playback and releases it on close, so music does not overlap
Games previews, GD Play, flashing, benchmarks or a game. GD Ripper loads music
from SD, IDE/CF or PC only, never from the disc. Its drive operations defer music
loads and preference saves; an already loaded track plays from RAM. Selecting
music during a rip without a cached track shows "queued" until the operation ends.
Audio adds CPU/AICA work; real-console throughput and sound checks remain pending.

The 1,058,444-byte WAV is loaded once into RAM per visit. Off retains this cache until you leave the app. Playback makes no
further storage reads. A missing/invalid track or unavailable audio stream
leaves the launcher usable; the music label reports it unavailable.

## Your own music

Replace `DS/apps/launch_app/music/menu.wav` with a RIFF WAV containing
**uncompressed 16-bit PCM, mono, 8–44.1 kHz**, at most **2 MiB**. Choose a short
track with matching beginning/end for a smooth loop. Example conversion:

```sh
ffmpeg -i your-track.ogg -t 24 -ar 22050 -ac 1 -c:a pcm_s16le menu.wav
```

Conversion does not automatically make a musical loop. Ogg/MP3 are input
formats for conversion; the launcher itself reads WAV. Keep a copy of custom
music before installing an update that supplies `menu.wav`.
