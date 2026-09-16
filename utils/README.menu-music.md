# DreamShell NeXT — Launcher 2.1.1 and GD Ripper 2.2.3 music

Launcher music is included in the complete NeXT 1.0 package. Follow the
[installation guide](../docs/installation.md) and back up custom music before
replacing program files. A working NeXT 3.0/3.1/3.2 boot disc can be reused.

**Launch App** now plays *After Hours*, an original 24-second synth loop made
for this project. The new **Y Music** control cycles **15%, 30%, 50%, 75%, 100%, Off**;
you can click it with a mouse or press **M** on a keyboard. The master volume
still applies. The preference persists across restarts. A `*` after the label
means the current selection is waiting to be saved, or the save failed.

Music plays in the launcher and GD Ripper, using the same track and preference.
GD Ripper has a visible **Y Music** button; **Y**, keyboard **M**, or a mouse click
cycles the level, including while a rip is running. Other apps remain silent.
Closing either app stops its own stream and frees its buffers.

The track loads on a worker thread. Turning music Off retains the RAM copy;
turning it back on does not reload the WAV. UI input and labels never wait for
music file reads, preference writes or audio transfers. Initial loading can still
take time on serial SD, but the controls remain available.

GD Ripper reads music only from an SD, IDE/CF or PC installation. It never reads
music from the disc being ripped and never sends CD-audio commands. Its service
thread excludes music loads and preference writes while probing, ripping,
recovering or verifying. Playback continues from RAM; preference saves wait
until the operation finishes. If ripping begins with no cached track, selecting
music displays **queued** until storage is available. A CD-only installation
reports music unavailable. Track loading uses up to 2 MiB of RAM; audio still
uses CPU and AICA transfers. Rip integrity, throughput and sound smoothness
need a real-console comparison with music on/off before release.

For custom music, replace `DS/apps/launch_app/music/menu.wav`. Use uncompressed
16-bit PCM WAV, mono, 8–44.1 kHz, no more than 2 MiB. The supplied theme is
22.05 kHz and about 1 MB. Further instructions and the theme's provenance are
in `DS/apps/launch_app/music/README.md`. Save a copy of custom music before
installing any update that supplies a replacement `menu.wav`.

Validation includes host execution of the launcher and music code, sanitizer
checks of WAV bounds and loop reads, saved preferences and resource cleanup,
and real-thread tests that block file/audio I/O while operating controls and
closing the app. The full build checks SH-4 compilation and runtime imports.
The updated modules still need that build and real Dreamcast music/ripping tests.
The already-tested 3.2 boot disc can be reused; these changes are in the app modules.
