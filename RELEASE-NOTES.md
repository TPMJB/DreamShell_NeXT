# DreamShell NeXT 0.9.1 — by TPMJB

A filesystem maintenance update to [0.9](https://github.com/TPMJB/DreamShell_NeXT/releases/tag/0.9).
The download includes the complete **DS** folder, bootloader CD image, and full
CD distribution, with all the apps and improvements from 0.9.

## What changed

- **FAT/exFAT validation:** reject malformed volume geometry, including invalid
  cluster counts and oversized FAT tables that could overflow size calculations.
- **exFAT volume labels:** limit reads to the label field, even when its stored
  length is corrupt.
- **Small FAT volumes:** correct formatting and mounting of supported small
  volumes and reject unsupported cluster counts.

These are ChaN's official **FatFs R0.16 patches 1 and 2**, applied to the shared
engine used by the core, bootloader, and ISO Loader. NeXT's storage adapter,
write/flush error handling, and asynchronous ISO Loader reads are retained.
This update does not add GPT support or change supported partition layouts.

Patch sources: [patch 1](https://elm-chan.org/fsw/ff/patch/ff16p1.diff),
[patch 2](https://elm-chan.org/fsw/ff/patch/ff16p2.diff).

## Updating from 0.9

1. Back up your current **DS** folder and preserve your saved settings, custom
   music, game dumps, and other personal files.
2. Extract **DreamShell-NeXT-v0.9.1.zip** and copy its **DS** folder to the root
   of your SD or IDE/CF device, replacing the matching program files.
3. Safely eject the device and boot DreamShell normally.

**You can keep your current NeXT 0.9 boot disc.** It can load the updated DS
folder. Burning the included bootloader CDI is optional and also updates the
bootloader's own filesystem reader with these fixes.

If you run DreamShell entirely from CD, burn **DreamShell-NeXT-v0.9.1.cdi** to
update that CD's contents. Burn CDI files as disc images. No BIOS flashing is
required. `host-tools/` stays on your computer.

The project version is **0.9.1**; the core/API remains **DreamShell 4.0.5 Beta 3**.
Individual application version numbers are unchanged from 0.9.

## Validation

Five new behavioral regression cases reproduce the original FatFs defects and
pass with the patches. Release checks cover FAT16/FAT32/exFAT file operations,
Linux FAT32/exFAT interoperability, application regressions, Dreamcast SH-4
compilation, and the complete package, including both disc images.
This maintenance revision has not been tested on a physical Dreamcast here.

`build-info.json` records the exact source and packaged versions. The attached
**SHA256SUMS** verifies the release ZIP; the ZIP also contains checksums for its
individual files.

DreamShell NeXT enhancements and branding by **TPMJB**, built on **SWAT's
DreamShell**, **KallistiOS**, **FatFs**, and the other credited projects. Original
licenses and notices are retained.

[Support development on Ko-fi](https://ko-fi.com/tpmjb).
