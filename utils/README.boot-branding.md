# K-UI — bootloader 3.3

This boot disc adds checked core loading, an immediate recovery menu, and
settings you can edit on SD. The corner badge on the SEGA startup screen uses
native-size vector artwork with a visor profile, large K-UI lettering and
`github.com/TPMJB`. The core splash uses the matching full artwork.
The new 3.3 CDI retains the tested 3.2 recovery menu and font fix. A maintainer
reported a freeze at the SEGA screen on the first K-UI disc. This remains an
open hardware issue; the sharper badge is not a confirmed fix for that freeze.

## Install

1. Extract `K-UI_bootloader_v3.3.cdi` from the complete K-UI ZIP.
2. Burn the CDI **as a disc image** onto a new CD-R using the same working
   image-burning method as your current DreamShell boot disc.
3. Boot the Dreamcast with the new CD-R and your existing SD card.
4. Optionally copy `boot.cfg.example` to `DS/boot.cfg` on SD and edit its settings.

The boot logo lives on the CD. Copying this ZIP or its artwork to SD does not
replace it. This is the K-UI FAT16/FAT32/exFAT-capable **3.3** bootloader;
it loads the compatible DS installation already on your SD/IDE device.
No BIOS flashing is required. To update apps and the core as well, follow the
[complete installation guide](../docs/installation.md).

The badge has a transparent background on the boot screen. Bootloader checks
cover the CDI bootstrap and embedded image; a newly burned CD-R still needs a
console boot check.

## Recovery menu

Version **3.2** fixes the garbled recovery text and old background seen when
booting without storage. This fix lives on the disc: copying files to SD cannot
update an already-burned 3.1 disc.

With no readable core, the screen explains where `DS/DS_CORE.BIN` belongs and
shows **X Rescan**. Insert SD, press X, then select a core and press A. Connect
IDE/CF before powering on. See the [layout previews](../docs/bootloader-preview/README.md).

Hold **Start** during startup to open the menu. By default the first normal
core boots immediately, with the original device discovery order. Opening
the menu, pressing a button during a countdown, cancelling a load, encountering
an error, or rescanning leaves the menu open until you choose to boot.

| Control | Action |
| --- | --- |
| Up / Down | Select a core; hold to scroll |
| A | Boot the selected core, or retry a failed load |
| B | Stop the countdown or cancel a load; hide Details while idle |
| X | Refresh mounted devices and reread settings |
| Y | Toggle Details: detection/load time, last byte count, settings file |
| Start | Keep the menu open, or cancel an active load |

Cancellation takes effect between bounded read requests and before the final
handoff. It cannot interrupt a storage driver that is still inside a request.
SD can be inserted before pressing X. Connect IDE devices before power-on;
rescan refreshes their volumes and does not provide IDE hot-plug support.

The menu shows the selected path, progress, and a specific failure reason.
Read failures never automatically retry or execute a partial image. Available
debug and emulator cores appear after normal cores for manual selection.

## Optional settings

Use a plain-text `boot.cfg`, at most 4 KiB. The first existing file is used:
SD partitions `/sd`, `/sd1`, `/sd2`, `/sd3`, then the corresponding IDE
partitions. On each partition, `/DS/boot.cfg` takes precedence over `/boot.cfg`.
Settings lookup is independent of the configured boot order. A malformed or
unreadable settings file holds the menu open with defaults and an error.
Remove the file to restore defaults, or edit it and press X to reread it.

| Setting | Default | Meaning |
| --- | --- | --- |
| `boot_order` | `auto` | Original discovery order, or e.g. `sd,ide,cd`; unlisted families follow |
| `core_path` | empty | Preferred absolute `.BIN` path, e.g. `/sd/DS/DS_CORE.BIN` |
| `fallback_path` | empty | Next candidate when the preferred file is absent; also listed for manual recovery |
| `autoboot` | `1` | `0` always opens the menu |
| `boot_delay` | `0` | Countdown in seconds, from `0` through `30` |
| `diagnostics` | `0` | `1` opens Details by default |

Device families are `sd`, `ide`, `cd`, `pc`, and `brd`. Core paths must name
a mounted supported device and must not contain `.` or `..` path components.
If neither configured candidate exists, the menu holds open and lists any
standard cores it finds. If the selected core exists but fails to load, choose
the fallback manually; failures always remain visible.

Normal discovery checks `/DS/DS_CORE.BIN`, `/DS_CORE.BIN`, `/1DS_CORE.BIN`,
`/DS/ZDS_CORE.BIN`, and `/ZDS_CORE.BIN` on each device. Legacy scrambled
`1DS_CORE.BIN` and gzip `ZDS_CORE.BIN` remain supported. Custom names are raw
cores. No new compression step is required.

## Loading and memory fixes

The loader loops over partial reads, checks sizes and close results, and
rejects early EOF or unexpected extra data. Executable sizes must be a
multiple of four bytes and between 4 bytes and 8 MiB. Existing gzip support
also checks decompression errors and the gzip trailer/CRC. Raw images receive
read/length checks; this does not add a raw-image checksum or authenticity check.

The font atlas is built in aligned system RAM with explicit 16-bit colors,
then uploaded synchronously to texture memory and freed. Padded glyph cells
and filtered scaling preserve thin strokes in the recovery screen. The legacy
animated logo is not loaded or drawn; the screen uses an opaque NeXT layout.
The scrambled-core decoder allocates its large index table on the heap and
handles allocation failure, removing its oversized stack allocation. Each
menu worker is joined before execution or rescan results are published.

Host tests inject short reads, read/close/allocation failures, damaged gzip
data, failed worker creation, and cancellation at the handoff. They also cover
default boot, settings/fallback selection, empty inventories, rescans, controller
edges, and menu bounds. CI additionally builds the SH4 binaries and validates
the generated CDI bootstrap and badge. Physical boot and controller behavior
still require a Dreamcast check.

## Artwork and source

Edit `resources/boot-disc-badge.svg`, then regenerate using:

```sh
python3 utils/build_boot_disc_branding.py --makeip /path/to/kos/utils/makeip/makeip
```

This host step requires CairoSVG and Pillow. It uses KallistiOS's `makeip`
encoder, validates every decoded MR pixel, enforces the image size limit, and
replaces only the logo region in a build copy of `resources/IP.BIN`. Normal builds use the committed
assets and do not need an image renderer. The package check reads the logo
back from the generated CDI and verifies the surrounding bootstrap bytes.
The complete packager also follows the ISO9660 boot-file entry in each CDI,
descrambles its contents and compares it with the compiled executable.

Branding and NeXT enhancements: **TPMJB**, https://github.com/TPMJB.
Built on SWAT's DreamShell, KallistiOS, FatFs, and their contributors' work.
Original notices are included in `LICENSE` and `NOTICE` and remain in the
source tree: https://github.com/TPMJB/DreamShell_NeXT.
