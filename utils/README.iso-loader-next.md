# DreamShell NeXT Games and ISO Loader

The complete NeXT 1.0 package includes **ISO Loader 2.0.6**, **Games 1.0.3**,
shared ISO Loader module **0.9.3**, ISOFS **1.8.1**, and standalone firmware
**0.9.2**. Keep the core, modules and firmware together when updating; see
[installation](../docs/installation.md). A working NeXT 3.0/3.1 boot disc can be reused.

## Games library

Games discovers the device Games folders used by GD Ripper: `/ide/Games`,
`/sd/Games` and `/pc/Games`. Keep each game's GDI descriptor and track files in
one folder. Reopen Games after adding a dump to refresh discovery.

- D-pad browses; A plays; X cycles list, two-column and gallery views.
- Y opens game setup; L/R changes pages.
- Start focuses the action bar. Left/Right chooses an action and A activates it.
  B or Up/Down returns to the game list.
- The analog pointer or a mouse selects visible buttons and game cells.

**Scan artwork** visits discovered games, keeps existing artwork and extracts
missing embedded `0GDTEX.PVR` thumbnails. It shows progress and a completion
report. B requests cancellation after the current disc; A/B dismisses the result.
A later scan retries unavailable thumbnails. This is not a box-art download
service. Missing images can reflect absent artwork or a read/write failure.
Covers are stored in `DS/apps/games_menu/covers`.

Audio previews use separate RAW/WAV CD-audio tracks from the game session
(track 4 onward). Track 2 is excluded because it is the low-density warning
track. Music encoded inside game data is not decoded; having artwork does not
imply having preview music. Wait about three seconds on a game selection.

## ISO Loader controls

D-pad moves between files and controls. A activates the selected item; opening a
folder updates its contents immediately. B goes up using the file browser's
parent-directory action. X jumps to the top controls, Y to the bottom actions,
and Start launches. Analog pointer/mouse input remains available alongside the
D-pad, with duplicate synthesized clicks suppressed.

**Baseline** chooses an unsaved diagnostic profile without deleting a preset.
It uses loader address `8c000100` for WinCE and `8ce00000` for other executables,
with CDDA and VMU emulation off. **Check** validates the image/track set and reads
the executable in DreamShell. **Play** checks that executable's CRC again in the
standalone loader before patches and handoff. This is not a whole-disc CRC test.

Preserve custom presets when upgrading. Games-specific presets can take
priority over an ISO Loader preset. Check the actual settings in the launch
report when comparing results between the two apps.

## Launch reports and compatibility

Reports are written to `DS/apps/iso_loader/last-launch.txt` and
`DS/apps/games_menu/last-launch.txt`. Copy them before another launch replaces
them. They describe preparation and errors returned to the shell; they cannot
record a crash after control has passed to the game.

- A missing/truncated track or invalid descriptor must be corrected before launch.
- An executable CRC mismatch stops the loader because the reads disagreed.
- A matching executable CRC followed by a black screen is still a compatibility
  failure; the checksum alone does not establish a successful boot.

**Bust-A-Move 4 on serial SD remains a known black-screen case with firmware
0.9.2. Code Veronica has a reported successful ISO Loader boot.** The WinCE SD
DMA-address fix did not resolve Bust-A-Move 4. See the
[hardware result record](../docs/compatibility.md) for the scope of testing.

## File and device limits

Raw 2352-byte GDI data tracks are unpacked into 2048-byte data sectors. The
standalone GDI reader expects standard `trackNN.bin` / `trackNN.iso` data names,
`trackNN.raw` / `trackNN.wav` audio names and zero descriptor offsets. Keep the
ripper-generated names and descriptor together. Other image formats have their
own compatibility limits.

Normal FAT/exFAT reads follow fragmented files. Specialized raw streaming
requires the requested area to be contiguous. ISO Loader rejects individual
files above its signed 2 GiB offset range. Serial SD bandwidth and the separate
WinCE DMA-streaming restriction remain. ELF firmware is installed under
`DS/firmware/isoldr/`; an old `sd.bin` is not required.
