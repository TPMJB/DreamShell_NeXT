# Compatibility and hardware results

This record distinguishes reported console results from source/host checks.
It is not a claim that every app, game, device or firmware write has been tested.

| Area | Evidence carried into 1.0 preparation | Remaining scope |
| --- | --- | --- |
| Integrated 1.0 source | All 167 host tests passed on 2026-09-16, including Linux FAT32/exFAT interoperability and recovery reporting; utility previews rendered successfully. | This is source preparation, not a completed 1.0 SH-4 build or fresh console test. Local LeakSanitizer detection was disabled for the host environment. |
| Serial SD ripping | The maintainer reports completed catalog-matching rips and successful Stop/Resume, including recovery of a scratched Sonic Adventure disc. | Recheck the combined 1.0 candidate before publishing. A damaged disc can remain unrecoverable. |
| GD Ripper 2.2.2 reporting | The maintainer reports the reporting issue fixed in its separate thread. Commit `fa96afbd6286233f7f61992f7f443063acb4a118` passed its host tests and SH-4 app build. | The 1.0 integration retains `/ide/Games` and `/sd/Games` destinations; record results against the combined build. |
| FAT16/FAT32/exFAT | The 0.9.1 build passed the host suite, Linux interoperability and SH-4 build with the FatFs R0.16 fixes. | Host images do not model SD/IDE timing or every card. |
| Code Veronica on serial SD | Reported boot through ISO Loader. | Do not infer every preset, CDDA or VMU-emulation combination works. |
| Bust-A-Move 4 on serial SD | Reported black screen after executable CRC verification and handoff with standalone loader 0.9.2. | Known compatibility limitation; investigation was deferred. |
| IDE/CF with GD-ROM retained | Paths and filesystem support are included. | Physical coexistence, sustained ripping and game loading await results on that hardware. |
| Bootloader 3.2 recovery display | Host checks cover the RAM font atlas/upload, TV-safe geometry, empty inventory and insert/rescan/boot sequence. | The 3.1 no-storage display fault was reported on console; confirm 3.2 rendering on a newly burned disc. |
| Settings and GD Play | Visible active-tab/navigation and disc polling fixes are included from 0.9. | Recheck save/reboot and lid/disc transitions on the combined candidate. |
| BIOS Flasher / Region Changer | Host failure-path tests and SH-4 compilation; physical Region Changer reads are implemented. | Flash writes require compatible hardware and a recovery path; comprehensive hardware validation is not claimed. |
| VMU Manager | Backup/copy/restore behavior has host tests and compiled modules. | Record actual VMU backup/restore and peripheral results. |
| NAOMI utilities | Existing Arcade, Cart Ripper and MIE JVS apps remain included. | Cart Ripper and MIE JVS need relevant arcade hardware; no new hardware validation is claimed. |

Build evidence: [0.9.1 complete build](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/34975888753)
and [GD Ripper 2.2.2](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35003894677).

For a useful compatibility report, include the NeXT build/source commit, game
revision, SD/IDE device, FAT32/exFAT, loader settings, last visible message, and
whether the game merely reached its title screen or was played. Include
`last-launch.txt` when available. Never upload a commercial game image.
