# Installing K-UI

The current published download is [NeXT 0.9.1](https://github.com/TPMJB/DreamShell_NeXT/releases/tag/0.9.1).
The `codex/k-ui` branch prepares **1.0**. A build artifact is a test build until a
versioned GitHub release is published. Use the files and notes from the same build.

## Files in a complete download

| Item | Purpose |
| --- | --- |
| `DS/` | Complete installation for your SD or IDE/CF device. |
| `K-UI_bootloader_v3.3.cdi` | Boot CD that loads `DS` from storage. |
| `K-UI-v1.0.cdi` | Full CD distribution for this prepared version. |
| `boot.cfg.example` | Optional boot menu/device settings; copy to `DS/boot.cfg` only when wanted. |
| `host-tools/` | Python verification and recovery tools for your computer. |
| `build-info.json`, `SHA256SUMS` | Source/version information and file checksums. |

## Storage and first installation

Use a Dreamcast serial SD adapter or compatible G1-ATA/IDE hardware. FAT16,
FAT32 and exFAT are supported on 512-byte-sector devices with an MBR primary
partition or a whole-device volume. GPT and devices at or above 2 TiB are not
supported. GDEMU's card is not ordinary writable SD storage for DreamShell.
See [the exFAT guide](../utils/README.exfat.md) for details.

1. Download a complete package and verify its outer ZIP against the release's
   `SHA256SUMS`. Extract the ZIP on your computer.
2. Copy `DS` to the root of your prepared device, so the core is at
   `DS/DS_CORE.BIN`. Copy the whole folder so apps, modules and firmware match.
3. Safely eject the device and connect it with the Dreamcast powered off.
4. Boot using a compatible NeXT boot disc. For a new installation, burn the
   included bootloader CDI **as a disc image**, not as a file on a data CD.

No BIOS flashing is required. The normal console core is `DS_CORE.BIN`;
the DEBUG and EMU variants are optional diagnostic files.

## Updating an existing installation

1. Finish or Stop current operations and wait for idle. Power down before
   removing storage. Back up the existing `DS` folder on your computer.
2. Preserve settings, game presets, covers, custom launcher music, VMU backups,
   and any other personal files. In particular, the supplied
   `DS/apps/launch_app/music/menu.wav` replaces custom music at that path.
3. Merge the new package's entire `DS` folder onto the device, replacing matching
   program files. Restore your personal files from the backup as appropriate.
   Do not restore old program modules over the new ones.
4. Safely eject, reboot, and check the app versions and `DS/NEXT_VERSION`.

Existing dumps need no conversion. Keep each dump's descriptor, tracks and all
`rip.*`, CRC, bad-sector and recovery sidecars together. Do not delete records
to make an incomplete dump appear complete.

## Do I need another CD?

A working NeXT **3.0 or 3.1** boot disc can load the updated `DS` folder.
Burning the included 3.3 disc is optional for those installations; it includes
the current boot reader fixes, recovery menu and TPMJB startup badge.
The 3.2 disc fixes garbled recovery text and the old background when no core is
found. Receiving that fix requires burning the new CDI; an SD update cannot
change the code on an existing CD-R.

An old bootloader that cannot read exFAT needs updating before it can boot an
exFAT device. Updating `DS` cannot change that disc's filesystem reader or logo.

If you run DreamShell entirely from CD, burn the full distribution CDI to
update the files on the disc. Files that must be saved still need writable storage.

## Rip a disc and find it in Games

GD Ripper prefers `/ide/Games`, then `/sd/Games`, then `/pc/Games`, depending on
the available device. Its device buttons open or create that device's `Games`
folder; each rip gets a separate disc-title folder. Other destinations remain
selectable. Games scans the same device `Games` locations. Reopen Games after
a completed rip so its library discovery runs again. Keep the GDI and tracks together.

See [the ripper guide](../utils/README.gd-verify.md),
[Games and ISO Loader](../utils/README.iso-loader-next.md), and
[utility controls](../utils/README.utility-apps.md) for app-specific instructions.

## If an update fails

Keep the new error message and logs. With the console off, restore your backed-up
`DS` folder to return to the prior installation. This does not undo BIOS/region
writes, VMU writes, or edits to game dumps; those have their own backup workflows.
Check that the bootloader selected the intended device/core if an old UI appears.

Bootloader 3.3 adds the K-UI name and disc artwork while retaining the tested 3.2
recovery/font fixes. Burn its CDI to update the disc branding. A working 3.2
disc still loads the new DS folder. No BIOS flash or saved-dump conversion is needed.
