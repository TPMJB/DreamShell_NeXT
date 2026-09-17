# K-UI 1.0

**Katana User Interface · by TPMJB**

![K-UI 1.0 — Katana User Interface](docs/launch/1.0/k-ui-release-banner.jpg)

A refreshed tools environment for your Dreamcast. Browse games, manage files
and VMUs, dump your discs, and check your storage from a coordinated interface.

**[Releases & downloads](https://github.com/TPMJB/DreamShell_NeXT/releases)** ·
[Install or update](docs/installation.md) ·
[What's new in 1.0](RELEASE-NOTES.md) ·
[App gallery](docs/screenshots/k-ui/README.md)

The maintainer has approved the hardware-tested 1.0 baseline for release.
The versioned download is **K-UI-v1.0.zip**, under tag **k-ui-1.0** once the
publishing workflow completes. Older `1.0` and `v1.0.0` tags are historical
milestones. Use the ZIP attached to the release, rather than GitHub's source ZIP.

## Inside K-UI

- **Launcher and Games:** controller navigation, app details, coordinated icons,
  game artwork and eligible CD-audio previews.
- **GD Ripper:** Stop/Resume, CRC checkpoints, TOSEC and Redump-derived catalogs,
  saved-file verification, targeted recovery and shared Games folders.
- **Storage:** FAT16, FAT32 and exFAT support in the core, bootloader and ISO Loader.
- **Everyday tools:** two-pane File Manager, VMU Manager, GD Play, Settings,
  Network, Speedtest and Memtest, plus maintenance and other standard utilities.
- **Bootloader 3.3:** corrected disc startup, readable recovery menu,
  insert-SD/rescan flow and crisp K-UI badge with `github.com/TPMJB`.
- **Original music:** five synth loops for Launcher and GD Ripper. Ripper music
  is stopped and unloaded before CRC verification, then restored afterward.

The retired Classic launcher is excluded from the package. Existing `DS` paths,
settings, game presets and saved-rip formats remain compatible.

[![K-UI app layout previews](docs/screenshots/k-ui/overview.png)](docs/screenshots/k-ui/README.md)

The gallery shows actual app layouts rendered on a host with sample data.
Launch artwork is promotional illustration. Neither is a hardware screenshot.

## Install and check compatibility

Copy the complete `DS` folder from the release ZIP to your supported SD or IDE/CF
device. Back up personal files first. **An already-working boot disc can be
reused** for an SD update; new installations can use the included
`K-UI_bootloader_v3.3.cdi`. No BIOS flashing is required.

**Known issue:** an intermittent GD-ROM dumping crash was reported with MDK2
and has not been reproduced reliably. Ten subsequent completed-rip resume/CRC
checks succeeded; those were not ten fresh disc dumps. Keep `rip.log`,
`kui-memory.log` and a photograph of any exception screen when reporting it.

Game and device compatibility varies. Read [hardware results and limits](docs/compatibility.md),
including the known Bust-A-Move 4 serial-SD black screen and untested hardware.

## Guides

- [Installation, upgrades and boot discs](docs/installation.md)
- [GD ripping, recovery and desktop verification](utils/README.gd-verify.md)
- [Games and ISO Loader](utils/README.iso-loader-next.md)
- [Utility apps](utils/README.utility-apps.md) and [VMU Manager](utils/README.vmu-manager.md)
- [exFAT](utils/README.exfat.md), [keyboard and folder picker](utils/README.input-ui.md)
- [Launcher](utils/README.launcher.md), [music](utils/README.menu-music.md) and [boot settings](utils/README.boot-branding.md)
- [Build instructions](docs/building.md) and [release checklist](docs/release-checklist.md)
- [Launch artwork and announcement drafts](docs/launch/1.0/README.md)

## Credits, source and support

K-UI enhancements and branding are by **TPMJB**. K-UI builds on **SWAT's
DreamShell**, **KallistiOS**, **FatFs** and the other credited projects. It was
previously named DreamShell NeXT; the repository URL is retained for continuity.
The project version is 1.0; inherited core/API version identifiers remain intact.

DreamShell-specific code and K-UI additions use [PolyForm Noncommercial 1.0.0](LICENSE),
except where separate terms apply. Third-party components retain their licenses.
See [NOTICE](NOTICE), [licensing](docs/licensing.md) and [contributing](CONTRIBUTING.md).

Downloads are free. [Optional support on Ko-fi](https://ko-fi.com/tpmjb) helps
with development, testing and documentation.
