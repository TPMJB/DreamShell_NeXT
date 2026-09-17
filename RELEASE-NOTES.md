# K-UI 1.0 — Katana User Interface, by TPMJB

**Full release, approved after the maintainer’s console retest.**

![K-UI 1.0](docs/launch/1.0/k-ui-release-banner.jpg)

The complete K-UI package brings together the redesigned apps, GD ripping and
recovery, exFAT support, bootloader improvements, and fixes from 0.9 and 0.9.1.
It also includes GD Ripper 2.2.2's corrected recovery reporting and the K-UI
attribution and contribution-policy cleanup.

## Changes since 0.9.1

- **K-UI identity:** new neon visor portrait, chrome wordmark, cyan/magenta app
  palette, boot splash, SEGA-screen badge, app icons and current previews.
  Existing DS paths, settings and saved-rip formats stay compatible.
- **Five original synth loops:** After Hours, Neon Circuit, Orbital Drift,
  Midnight Vector and Chrome Horizon. A random track is selected per Launcher
  or GD Ripper visit and loops from RAM. Muting retains it. Additional tracks
  use storage, not additional playback buffers; no track changes during a rip.
  CRC verification stops and unloads music, then restores it afterward.

- **Responsive music controls:** Launcher 2.2.0 keeps the track in RAM when muted,
  moves loading and preference saves off the UI path, and adds 75%/100% levels.
  GD Ripper 2.2.4 uses the same optional music and Y/M control. Music playback
  reads from RAM, with file loading/saving deferred during drive operations;
  no music is loaded from CD. App updates can reuse an already-working boot disc.
  Memory and readback diagnostics are included for investigating failures.

- **Bootloader 3.3:** includes the corrected early startup that reaches the
  recovery menu and boots after SD insertion on the maintainer’s hardware. It
  adds the crisp `github.com/TPMJB` badge and retains the recovery
  fixes: corrects the font upload, removes the legacy
  animated background, and adds a TV-safe K-UI missing-storage screen with SD
  insertion/rescan instructions. Burn the updated bootloader CDI to receive
  this fix; it cannot be installed by updating the SD card alone.

- **Accurate recovery totals:** GD Ripper shows originally flagged, recovered
  and unresolved sectors across recovery sessions, Start / Resume and
  verification. Saved repairs remain counted after reopening. Invalid records
  report unavailable counts; an interrupted final checkpoint offers **Finish
  recovery** even when no unresolved sectors remain. This changes reporting and
  finalization handling, not the optical-drive retry algorithm.
- **App stability and controls:** corrected File Manager startup, Lua object
  cleanup and binding-module lifetime; checked app initialization and startup
  logging; shared stick-pointer handling including GD Ripper and VMU Manager;
  confirmation-selection fixes and suppression of duplicate button actions.
  Earlier app reload and directory-scan fixes are included. The Classic launcher
  is excluded from the package; shell messages and save descriptions use K-UI.
- **Release packaging:** the full build is manual and defaults to an artifact.
  Publishing from reviewed `master` requires explicitly selecting publication
  and refuses an existing release tag or a source branch that changed mid-build.
- **Build attribution:** release cores contain a readable TPMJB/K-UI record with
  the source commit; the package contains the same record and original notices.
  Official packaging reports missing notices or mismatched branding directly.
  These records do not affect runtime behavior.
- **Current guides and contribution terms:** installation, upgrades, game
  compatibility and hardware limits are documented together. Contributors keep
  their copyrights; the inherited CLA granting additional commercial relicensing
  rights is removed from K-UI's contribution process. Upstream/third-party
  licenses and required notices remain applicable.

## Included apps and core improvements

| Component | Version | Included behavior |
| --- | --- | --- |
| GD Ripper | 2.2.4 | Checkpoints, Stop/Resume, catalog CRCs, optional saved-file scans, targeted recovery corrected totals and optional RAM-backed music. |
| Launcher | 2.2.0 | App list, details, coordinated icons and optional original background music. |
| Games | 1.0.3 | Library views, embedded artwork extraction, eligible CD-audio previews and shared Games paths. |
| ISO Loader | 2.0.6 | D-pad/pointer navigation, immediate folder refresh, image/executable checks and launch reports. |
| VMU Manager | 2.1.0 | Labeled slots, individual/bulk save copying and explicit full-card backup/restore. |
| Settings / GD Play | 2.0.2 | Clear current tab and save status; stable disc polling and menu return. |
| File Manager | 2.0.1 | Two panes, controller actions and checked copy/cancel handling. |
| BIOS Flasher | 3.0.1 | Verified backups, full-bank bounds and post-write comparison. |
| Region Changer | 2.0.1 | Physical flash reads, draft settings, checked backup/restore and write verification. |
| Speedtest / Memtest / Network | 2.0.1 | Diagnostic options/reports and clearer connection/server controls. |
| Bootloader | 3.3 | Checked loading, recovery menu, optional storage settings and TPMJB badge. |
| Standalone ISO Loader firmware | 0.9.2 | ELF payloads and retained WinCE SD DMA-address correction. |

The complete standard app set remains installed, including utilities for other
hardware. FAT16/FAT32/exFAT support and ChaN's FatFs R0.16 patches 1 and 2 are
retained in the core, bootloader and ISO Loader. The QWERTY keyboard, input fixes,
startup chime and bounded drive handling are included.

## Install or update

1. Stop current operations, wait for idle and power down. Back up your existing
   `DS` folder, settings, presets, covers, custom music and personal files.
2. Extract **K-UI-v1.0.zip**. Merge its entire **DS** folder onto the
   root of your SD or IDE/CF device, replacing matching program files.
3. Preserve/restore personal files as needed. Remove the retired `DS/apps/main`
   folder if it remains from an older installation. The supplied
   `DS/apps/launch_app/music/menu.wav` replaces custom music at that path.
4. Safely eject and reboot. Keep existing dumps and their recovery/CRC sidecars;
   no dump conversion is needed.

**You can keep your already-working boot disc, including the fixed K-UI 3.3 disc.**
It can load the updated DS folder. Burning **K-UI_bootloader_v3.3.cdi** is optional for those users
and updates the disc's own reader, recovery menu and branding. Older loaders
without exFAT support need updating to boot an exFAT device.

For running entirely from CD, burn **K-UI-v1.0.cdi**. Burn CDI files
as disc images. No BIOS flashing is required. `host-tools/` stays on your computer.
The ZIP includes `installation-guide.md`, app guides and `boot.cfg.example`.

## Ripping into Games

GD Ripper prefers **/ide/Games → /sd/Games → /pc/Games** according to available
devices. Each device button opens or creates its Games folder, and each disc
gets a separate folder. Games discovers those same locations. Other destinations
remain selectable; existing files and dumps are preserved.

## Validation and limitations

**An intermittent GD-ROM dumping crash remains unresolved.** It was reported
with MDK2 and has not been reproduced reliably. Ten later completed-rip resume
and CRC checks succeeded with music off/on; these were not ten fresh disc dumps.
The available logs do not establish a memory-exhaustion cause. Preserve
`rip.log`, `kui-memory.log`, the disc revision and a photo of the full exception
screen if it happens again.

The maintainer reports the latest app/navigation retest working apart from
that intermittent dumping failure. The tested baseline is
[`62a5fbc`](https://github.com/TPMJB/DreamShell_NeXT/commit/62a5fbce9e0af69a48f726cd62f77147491d0c44),
with a [successful complete Dreamcast build](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35180819886).
The publishing workflow checks the final release commit again.

The release pipeline requires the host regression suite, Linux FAT32/exFAT
interoperability, SH-4 app/loader compilation, full linking, runtime-import checks
and package validation, including both CDI images and attribution records.
The source commit and successful run are attached to published release notes;
`build-info.json` records the packaged versions and `SHA256SUMS` verifies files.

Hardware results and open limits are in [compatibility](docs/compatibility.md). Bust-A-Move 4 on
serial SD remains a documented black-screen case; Code Veronica has a reported
successful ISO Loader boot. IDE/CF coexistence and firmware writes need suitable
hardware validation. No universal game-compatibility claim is made. Catalog
matches verify the represented track files, not subchannel/lead-in capture.

The project version is **1.0**; the core/API remains **DreamShell 4.0.5 Beta 3**.
The historical **v1.0.0** tag predates 0.9 and is not this release. The earlier `1.0` tag is also
preserved; this K-UI release uses `k-ui-1.0`.

K-UI enhancements and branding by **TPMJB**, built on **SWAT's
DreamShell**, **KallistiOS**, **FatFs**, and the other credited projects.
PolyForm Noncommercial and separate third-party licenses remain in force.
See [licensing](docs/licensing.md) and [NOTICE](NOTICE) for scope and attribution.

[Support development on Ko-fi](https://ko-fi.com/tpmjb).
