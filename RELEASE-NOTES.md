# DreamShell NeXT 1.0.0 — by TPMJB

The first complete NeXT release brings the tested development branches into one
download. It is based on SWAT's DreamShell and KallistiOS; their original credits
and licenses are retained. NeXT's release number is separate from the underlying
DreamShell 4.0.5 Beta 3 core/API version shown by some older applications.

## Install

1. Finish or Stop any current operation and wait for idle. Power down before
   moving the SD/CF card to your computer.
2. Extract **DreamShell-NeXT-v1.0.0.zip**. Copy/merge its entire **DS** folder onto
   the card root and replace matching files. Keep your previous DS folder as a
   backup; preserve game dumps and personal files. Safely unmount the card.
3. Boot normally. A working NeXT exFAT bootloader **3.0** CD can keep loading the
   new core: the new splash and chime are inside `DS/DS_CORE.BIN`.

For a new installation or an older bootloader, burn the included
`DreamShell_bootloader_v3.0.cdi` **as a disc image**. This CD loads the DS folder
from SD/IDE and supports FAT16, FAT32 and exFAT. The included
`DreamShell-NeXT-v1.0.0.cdi` is the complete CD-based DreamShell distribution.
The refreshed bootloader has NeXT text branding; replacing your current 3.0 CD
is optional. No BIOS flashing is required for this update.

Copy the whole DS folder together: the keyboard, launcher and ripper depend on
the matching core. `host-tools/` stays on your computer. `DEBUG_DS_CORE.BIN` and
`EMU_DS_CORE.BIN` are optional diagnostic/emulator variants, not replacements
for the normal `DS_CORE.BIN` on a console.

## Included

- **New boot splash and original chime.** Navy/cyan DreamShell NeXT branding,
  with TPMJB attribution. Existing startup-sound enable/volume settings apply.
  The boot texture uses 1 MiB of PVR memory, down from 2 MiB. Audio playback now
  bounds its final buffer read and frees each stream on completion or failure.
- **Launcher 2.0.2.** A contained application list, descriptions that wrap at
  word boundaries, previews and controller navigation.
- **QWERTY onscreen keyboard.** Text preview, insertion cursor, Shift/symbols,
  Backspace, Done and Cancel. Modal input fixes duplicate characters.
- **GD Ripper 2.2.0.** Automatic disc-title detection, directory browsing and
  saved-dump selection; checkpoints, Stop/Resume, logs, streaming CRC checks,
  bundled TOSEC/Redump-derived catalog, optional storage read-back and targeted
  sector recovery. Recovery keeps backups and rechecks repaired output.
- **exFAT support** through the bootloader, core, GD Ripper's filesystem service
  and ISO Loader 0.9.0, alongside FAT16/FAT32. See `exfat-guide.md` for supported
  partition layouts, file-size limits and storage requirements.
- **All standard apps, modules and firmware** from the complete release build,
  plus desktop verification/recovery tools and the installation/test guides.
- **Selected upstream fix:** query the initialized SD/W5500 SPI interface
  instead of interpreting an SPI function's error code. The existing kernel pin and
  GD-ROM timeout fix are preserved. See `upstream-review.md` for the review.

## Controls and verification

The launcher uses D-pad/stick to select and A to open. In the ripper choose
**Destination** to browse folders; open an existing dump and select **Select this
dump** to resume or verify it. See `input-ui-guide.md` for keyboard controls.

Normal CRC checking hashes the disc stream/checkpoint without another slow SD
read. **Storage read-back** checks the actual saved files. **Advanced CRC** checks
data-sector integrity and supports targeted rereads. A whole-track catalog CRC
cannot itself identify which sector differs. Preserve `verify.log`, `rip.log`
and sidecars if the console and a PC disagree before another repair attempt.

FAT32 and exFAT share the current filesystem implementation. A useful remaining
hardware regression check is one known-good FAT32 rip with a clean Stop/Resume
and independent PC hashes. Existing matching dumps need not be ripped again.

## Validation and limits

The release pipeline runs the host regression suite, Linux FAT32/exFAT
interoperability checks, sanitizer checks for input/audio, and the full SH-4
build. It checks the archive's required apps, boot discs, firmware and embedded
boot assets before publishing. `build-info.json` records the source commit and
component versions; `SHA256SUMS` in the ZIP covers its files.

The launcher and GD ripping from exFAT have been exercised on a physical
Dreamcast. Successful PC/console CRC reruns do not conclusively explain the
earlier intermittent read-back discrepancy; diagnostic capture remains enabled.
The new keyboard, every other app, game-loading combinations and future
GD-ROM-plus-IDE/CF setups still need their own hardware checks. This release
does not change GDEMU firmware or add EXT4. Serial SD speed remains limited by
the interface.

DreamShell NeXT enhancements and branding by **TPMJB**. Built on **SWAT's
DreamShell**, **KallistiOS**, **FatFs**, and the other credited projects. See
`DS/doc/LICENSE`, `DS/doc/NOTICE`, and the source tree for original notices.
