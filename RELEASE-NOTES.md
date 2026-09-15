# DreamShell NeXT 0.9 — by TPMJB

This complete release collects the bootloader, launcher/music, ISO Loader/Games,
VMU Manager and utility app work, plus the screen-edge and Region Changer fixes.
The project build number is 0.9; the underlying core/API remains DreamShell
4.0.5 Beta 3. Individual applications retain their own version numbers.

## Install

1. Finish or stop any current operation and wait for idle. Power down before
   moving the SD/CF card to your computer.
2. Extract **DreamShell-NeXT-v0.9.zip**. Copy the complete **DS** folder to the
   device root, replacing matching files. Preserve your existing configuration,
   custom music, game dumps and other personal files; keep the previous DS folder
   as a backup. Safely unmount the device.
3. Boot normally. A working NeXT exFAT bootloader 3.0 CD can load the new core.
   The included bootloader CD adds the newer boot menu and recovery controls.

The ZIP contains both the bootloader CDI and **DreamShell-NeXT-v0.9.cdi**, the
complete CD distribution. Burn CDI files as disc images. No BIOS flashing is
required to install this build. `host-tools/` stays on your computer.
Use `DS_CORE.BIN` on a console; DEBUG/EMU variants are included for diagnostics.

## GD Ripper and Games

GD Ripper 2.2.1 defaults to **/ide/Games** when IDE/CF is connected, otherwise
**/sd/Games**, then **/pc/Games**. It creates the Games folder if needed and
places each disc in **Games/<disc title>/** with its GDI and track files.
The destination buttons open or create Games on the selected device. You can
still go Up and choose another directory or an existing dump to resume.
An unavailable or unwritable destination stops the operation; no existing
file is replaced to create the folder.

Games uses the same default Games folder on SD and IDE. Open Games after a rip
to scan it automatically. Existing lower-case `games` folders work on FAT32 and
exFAT. A saved custom `games_path` still takes precedence; select that location
in the ripper or restore the Games default to keep them aligned. PC output is
for host storage and is not a Games-menu device.

Stop/Resume, recovery checkpoints, streaming CRCs, the bundled disc catalog,
optional storage read-back and targeted sector recovery are retained. See
`host-tools/README.md`, `input-ui-guide.md` and `readback-guide.md` for details.

## Screens and Region Changer

File Manager, GD Play, Settings, BIOS Flasher, Region Changer, Speedtest,
Memtest and Network keep their controls and labels at least 32 pixels inside
the 640x480 screen. Fonts keep their native sizes. File pickers and progress
bars follow the same bounds, and Memtest's controller legend is above the edge.

Settings 2.0.2 keeps the current tab filled in cyan and names the page in the
heading. Up from the first setting returns to that tab; Left/Right switches
pages while staying on the tab row, and Down or A enters its settings.

GD Play 2.0.2 fixes repeated Reading/Ready flashing caused by normal drive
motion being mistaken for a disc change. Disc details remain visible until
the media changes or Read disc again/X is selected. Metadata is repainted as
one complete screen update, and the disc illustration fits its native viewport.
Menu/B/START return remains available.

Region Changer reads the console's physical factory settings independently of
the boot BIOS's read return values or temporary region override. It shows the
stored region, language, video standard and swirl, with unknown fields clearly
identified. Reading and backups need no hardware modification. Writes still
require a recognized partition layout and compatible hardware, with a verified
backup before erase and comparison against the physical flash afterward.
Changes take effect after restart. Menu/Back remains available while idle.

## Other included work

- Bootloader branding, checked loading, boot settings and recovery controls.
- Launcher with the original After Hours music loop and saved music level.
- ISO Loader/Games navigation, metadata, artwork and launch-error fixes,
  including the WinCE SD read-address correction.
- VMU Manager with explicit save operations, Copy all saves and full-card images.
- The complete core, standard apps, modules, firmware, onscreen keyboard,
  boot splash/chime, FAT32/exFAT support and desktop verification tools.

App-specific guides are included beside this file. `build-info.json` identifies
the exact source and packaged app versions. `SHA256SUMS` covers the ZIP contents.

## Validation

The build runs host regression tests, FAT32/exFAT interoperability checks,
screen bounds and font previews, then compiles the Dreamcast modules and complete
distribution. Packaging checks all app XML against source, native modules,
boot discs, firmware, music, version data and embedded boot assets.
Host previews are not hardware emulation. The GD Play polling and Settings
navigation fixes have regression coverage, but this final revision has not
been re-tested on a physical console here. Flash-write tests use simulated hardware.

DreamShell NeXT enhancements and branding by **TPMJB**, built on **SWAT's
DreamShell**, **KallistiOS**, **FatFs**, and the other credited projects. Original
licenses and notices are retained in `DS/doc/LICENSE`, `DS/doc/NOTICE` and source.
