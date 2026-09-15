# DreamShell NeXT utility apps 2.2

The eight redesigned utilities now keep all controls, labels and file pickers
within a 32-pixel inset on the native 640x480 display. Font sizes are unchanged.
Progress bars follow the actual layout width. This fixes TV overscan hiding
the footer and bottom actions.

When this guide is bundled with `ISO-Loader-Guide.md`, follow that guide for the
combined update's contents and installation. The utility-only package below
also remains available from its own branch workflow.

Merge this update's **DS** folder into the DS folder on your SD/IDE device.
Replace matching files. Keep the current boot disc. This update contains only
app files, a shared font, and launcher icons; it preserves your current core,
ISO Loader, Games, VMU Manager, bootloader, and GD Ripper executables.
Requires the DreamShell NeXT core with the modal keyboard/input fixes.

## File Manager

Two panes with visible paths, labelled actions, and direct controller operation.
D-pad up/down selects items; left/right switches panes. A opens the selected
item. B goes up a folder. X copies to the other pane. Y toggles action-button
navigation. START returns to the menu. In rename/new-folder prompts, X opens
the keyboard and A confirms after editing. B cancels copies between chunks.

Copy never overwrites an existing destination; rename the destination or choose
another folder. Parent entries, storage roots, and copying a folder inside
itself are rejected. A cancelled directory copy retains files already completed.

## GD Play

Disc details are read in a separate worker with bounded drive commands.
A activates the selected button; X rereads the disc; B or START returns to the
menu. The Menu button remains visible even with no disc or a read error.
Exit can wait for the current drive command (up to five seconds) to finish.
Playing a game closes DreamShell; returning from within a retail game still
requires a console reset. This update does not add in-game return hooks.

## Settings

Display, Sound, Startup, Clock, and System tabs use the same controls. Up/down
selects; left/right adjusts; A advances; X reverses; Y changes tabs; START saves.
B returns to the menu, with a discard prompt if edits are pending.

Changes stay in a draft until Save. Save verifies the configuration in boot
load order and reports failure when it cannot confirm it. Existing VMU-first
storage priority is preserved. Output/startup settings apply after restart.
Muting preserves sound-effect preferences. Clock fields use local console time;
Set console clock applies them independently from saving the network sync time
zone. The time zone is applied by NTP when syncing; saving it alone does not
shift the current console clock.
Restore defaults edits a draft and does not delete saved files from VMUs.

## BIOS Flasher 3.0

Detects the chip and visible hardware bank, compares complete images, creates
verified backups and writes exact full-bank images. Writing requires a verified
backup in an SD/IDE/PC folder before any erase, rechecks the chip and current
contents, then verifies the complete programmed bank. Backup names are unique.
Partial writes and sectors spanning another bank are rejected. Chip capacities
above 2 MiB require a compatible sector layout; chip-wide erases are prohibited
when they would erase another bank. Physical bank selection remains the user's
hardware switch. No image signature check can certify a custom BIOS as bootable.

Stop transfers before writing. Keep power and the bank switch unchanged during
erase/write/verification. Firmware phases cannot be interrupted; errors identify
the failing phase and retain the original backup. Do not reboot on a failed
write merely because the application has returned to its controls.

## Region Changer 2.0

Region, language, broadcast mode and black swirl are draft edits with an
on-console summary. Apply preserves every other factory byte. Factory restores
require an exact 8 KiB file and recognized field values. Advanced offers separate
backup/restore/clear actions for Block 1 and the game-settings partition. Every
erase requires a verified backup and a subsequent full read-back comparison.
Reads use the physical flash through its uncached mapping, so boot BIOS read
return conventions and temporary region overrides cannot misreport the stored
settings or corrupt a backup. Unknown fields are shown as unknown; A retries
the read. Opening the app rereads the console. Reading requires no modification.
An unavailable or unexpected BIOS partition layout permits viewing and backup,
but blocks writes. Write verification checks physical flash even when the BIOS
returns zero on success. Factory writes still require suitable hardware; the
app does not bypass write protection.

## Speedtest 2.0

Three modes: filesystem write + read-back verification, existing-file reads
(including GD-ROM), and raw read-only SD/IDE device tests. Select 2/8/32 MiB and
1/3/5 passes. The 256 KiB buffer is owned RAM with a pattern that varies by file
offset. Test files are created exclusively, never replace an existing file, and
are deleted after completion, failure or cancellation. Cleanup failures show the
remaining filename. B cancels between chunks.

Timings use 64-bit nanoseconds. MiB/s includes IO calls and close/flush, while
pattern generation, comparison and GUI work are outside IO timing. Reports also
include wall time, byte counts, completed passes and verification status. Caches
remain enabled; compare the same modes and sizes. Partial failed/stopped runs
are labelled and are not successful benchmark results. Save report writes and
verifies a uniquely named text file. Sample preview speeds are illustrative.

## Memtest 2.0

The existing memory test engine and OCRAM assembly are unchanged. A toggles a
region; Left/X shows its retained results. Options chooses Quick (data/address
bus) or Full (also device patterns), 1/3/10 passes, stop-on-first-failure and a
report folder. First failures survive later passes. Reports include each pass,
subtest, failure address and expected/actual values; engine errors are distinct
from detected data failures. Saving verifies the report file.

The engine backs up/restores tested chunks. It excludes the live RAM execution
island, so the UI does not claim exhaustive coverage. Video and sound may pause.
B takes effect between regions after the current hardware test returns.

## Network 2.0

Shows the actual active interface, IPv4 address, subnet and gateway, plus FTP
and HTTP status and connection URLs. Refresh reads current state instead of an
old environment string. Ethernet and dial-up remain mutually exclusive. Server
activity must stop before changing the connection. Dial-up uses the existing
PPP defaults, with the corrected --shut disconnect option; its connection
attempt may take over a minute and must return before further actions.

Options edits startup connection and NTP preferences in a draft, with explicit
save and verification in the same VMU-first order used at boot. FTP's start
folder is an initial directory, not a filesystem access restriction. These
existing guest FTP and HTTP-control services should be used on a trusted LAN.
The session setting for background servers controls only services started by
this app; services already running on entry are not stopped automatically.
Server modules remain resident so their backend threads retain valid code.
Custom addressing and dial-up profiles still use the existing core/console
configuration. Interface-up status does not prove Internet or DNS reachability.

## Validation and test scope

The test-branch workflow compiles and links the SH-4 modules, runs host behavior
checks and uploads this app update only. It does not create a release or tag.
Host tests exercise firmware sequencing and failure phases, exact bank bounds,
factory-byte preservation, short IO, unique-file creation, rejected backups,
64-bit timing and UI export references. Layout previews use actual XML and the
shipped font, with sample runtime values, not hardware emulation.

Before any full release, test controller navigation/exit/reopen, storage reads,
backup/report files, connection/server start-stop, and draft-save/reopen on the
console. Test firmware writes only on hardware with a known recovery path.
The initial app updates also still need console checks for FAT32/exFAT copy,
GD Play drive transitions and Settings persistence across reboot.
