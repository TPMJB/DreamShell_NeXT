**☕ [Support TPMJB's work on DreamShell NeXT on Ko-fi](https://ko-fi.com/tpmjb)**

Optional tips help support development, testing and documentation. Downloads remain free.

# DreamShell NeXT — by TPMJB

A Dreamcast tools environment inspired by Swat's DreamShell
and KallistiOS. NeXT adds exFAT boot/storage support, a controller-friendly app
launcher, a QWERTY keyboard and directory browser, and reliable GD ripping with
checkpoints, catalog verification and targeted recovery.

**[Download the full NeXT 0.9.1 release](https://github.com/TPMJB/DreamShell_NeXT/releases/tag/0.9.1)**
— includes the complete DS folder, boot CDs, firmware and desktop tools.
Read [the published installation and release notes](https://github.com/TPMJB/DreamShell_NeXT/blob/0.9.1/RELEASE-NOTES.md)
before updating. [Browse the released 0.9.1 source](https://github.com/TPMJB/DreamShell_NeXT/tree/0.9.1).

**1.0 is being prepared on `master`.** This branch includes the complete 0.9.1
source, GD Ripper 2.2.2, and the attribution and documentation cleanup. The
[prepared release notes](RELEASE-NOTES.md) and [release checklist](docs/release-checklist.md)
describe the candidate; 0.9.1 remains the published stable download.

[![DreamShell NeXT 0.9 overview: Launcher, GD Ripper, Games, Settings, VMU Manager and File Manager](docs/screenshots/0.9/00-release-overview.png)](docs/screenshots/README.md)

**[Explore all 14 app screens →](docs/screenshots/README.md)**
— click any image in the gallery to open it at full size.
These are layout previews from the released 0.9 source, with sample data.

## Included in the 1.0 source

- **GD Ripper 2.2.2:** disc-title detection, Stop/Resume, CRC checkpoints, TOSEC
  and Redump-derived catalogs, optional saved-file scans, and targeted recovery.
  Recovery reports original, recovered and unresolved totals across sessions.
- **Shared Games folders:** rip to `/ide/Games`, `/sd/Games` or `/pc/Games` and
  browse the same locations in Games. Custom destinations remain available.
- **FAT16/FAT32/exFAT:** boot, shell and ISO Loader storage support, including
  the FatFs R0.16 maintenance fixes from 0.9.1.
- **Games and ISO Loader:** controller navigation, artwork extraction, eligible
  CD-audio previews, image checks and launch reports. Compatibility varies by
  game and storage hardware.
- **NeXT utilities:** File Manager, VMU Manager, GD Play, Settings, BIOS Flasher,
  Region Changer, Speedtest, Memtest and Network, plus the other standard apps.
- **Bootloader 3.2 and launcher music:** checked core loading, recovery menu,
  optional boot settings, TPMJB branding and the original *After Hours* loop.

A working NeXT 3.0/3.1 boot disc can load the updated DS folder. The project
release number is independent of the DreamShell **4.0.5 Beta 3** core/API and
individual app versions. Check [hardware results and limits](docs/compatibility.md)
before assuming a particular game or device is supported.

## Guides

- [Install, upgrade, preserve settings, and choose a boot disc](docs/installation.md)
- [GD ripping, recovery and desktop verification](utils/README.gd-verify.md)
- [Games and ISO Loader controls](utils/README.iso-loader-next.md)
- [Settings, GD Play and maintenance utilities](utils/README.utility-apps.md)
- [VMU backup, copying and restore](utils/README.vmu-manager.md)
- [exFAT requirements and limits](utils/README.exfat.md)
- [Keyboard and folder picker](utils/README.input-ui.md)
- [Bootloader configuration](utils/README.boot-branding.md)
- [Launcher controls](utils/README.launcher.md) and [music](utils/README.menu-music.md)
- [Saved-read diagnostics](utils/README.readback-diagnostic.md)

## Build

Use **Actions → Full NeXT release → Run workflow**, select `master`, and leave
**Publish a GitHub release** unchecked to produce a complete test artifact.
Publication is a separate explicit choice. Normal pushes run host checks and
do not start a full toolchain/Dreamcast build.

[Build instructions](docs/building.md) cover Actions, local Ubuntu builds and
the exact release outputs. `master` is the current integration source; use the
`0.9.1` tag when reproducing that published release. The old `v1.0.0` tag is a
historical development milestone, not the upcoming 1.0 release.

## Licensing and contributions

DreamShell-specific code and NeXT additions use
[PolyForm Noncommercial 1.0.0](LICENSE), except where separate terms apply.
Third-party components retain their own licenses. Preserve the required
[notices](NOTICE); see [licensing and attribution](docs/licensing.md) for scope.

Contributors retain their copyrights. NeXT uses the applicable existing licenses
and does not require a separate CLA or commercial relicensing grant. See
[CONTRIBUTING.md](CONTRIBUTING.md) for submissions and bug reports.
