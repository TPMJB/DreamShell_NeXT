# DreamShell NeXT utility apps 2.0

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

## Validation

The branch workflow is configured to cross-compile and link the SH-4 modules
and run host behavior checks. It uploads a test artifact only; it does not
create a release or tag. The Dreamcast build is pending.

Local validation: 95 tests ran successfully with one skipped because Linux
exFAT interoperability tools were unavailable. AddressSanitizer and
UndefinedBehaviorSanitizer ran with leak detection disabled because this host
cannot access the process information LeakSanitizer requires. The three app
modules also passed host C syntax checks against project and pinned KOS headers,
with platform shims; those checks do not replace SH-4 compilation.

Layout previews use the shipped fonts and actual XML, with sample runtime data.
Real hardware checks still needed: both FAT32/exFAT copy and cancellation;
rename keyboard; GD Play insert/eject/error/exit/reopen and disc boot; Settings
save/reopen/reboot, volume preferences, startup app and time zone.
