# DreamShell NeXT — Launcher 2.1.0 music

Launcher music is included in the complete NeXT 1.0 package. Follow the
[installation guide](../docs/installation.md) and back up custom music before
replacing program files. A working NeXT 3.0/3.1 boot disc can be reused.

**Launch App** now plays *After Hours*, an original 24-second synth loop made
for this project. The new **Y Music** control cycles **15%, 30%, 50%, Off**;
you can click it with a mouse or press **M** on a keyboard. The master volume
still applies. The preference persists across restarts. A `*` after the label
means the current selection could not be saved to the card.

Music plays while browsing the launcher. Opening an application or shortcut
stops it and frees its buffers; returning to the launcher restarts it.
This keeps it out of Games audio previews, GD Play, game launches and utility
operations. The track is loaded into RAM once per visit, with no ongoing
card reads during playback. Missing or unsupported audio leaves the menu usable.

For custom music, replace `DS/apps/launch_app/music/menu.wav`. Use uncompressed
16-bit PCM WAV, mono, 8–44.1 kHz, no more than 2 MiB. The supplied theme is
22.05 kHz and about 1 MB. Further instructions and the theme's provenance are
in `DS/apps/launch_app/music/README.md`. Save a copy of custom music before
installing any update that supplies a replacement `menu.wav`.

Validation includes host execution of the launcher and music code, sanitizer
checks of WAV bounds and loop reads, saved preferences and resource cleanup,
SH-4 compilation, and checks that every module import is exported by the
pinned KOS/DreamShell core. A real Dreamcast test is still needed for sound
quality, smooth playback while browsing and repeated app transitions.
