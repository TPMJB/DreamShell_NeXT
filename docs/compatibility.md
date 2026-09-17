# Compatibility and hardware results

This record distinguishes reported console results from source/host checks.
It is not a claim that every app, game, device or firmware write has been tested.

| Area | Evidence carried into 1.0 | Remaining scope |
| --- | --- | --- |
| Integrated 1.0 baseline | Commit `62a5fbc` passed the [complete Dreamcast build](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35180819886), including host checks, Linux FAT32/exFAT, SH-4 compilation/linking and package validation. The maintainer reports the latest app retest working except the intermittent dumping crash and approves full release. | The final publishing workflow validates the release commit again. This general report does not establish every individual firmware/peripheral scenario. |
| Serial SD ripping | The maintainer reports completed catalog-matching rips and successful Stop/Resume, including recovery of a scratched Sonic Adventure disc. | An intermittent MDK2 dumping crash remains unresolved. A damaged disc can remain unrecoverable. |
| GD Ripper recovery reporting | The maintainer reports the reporting issue fixed in its separate thread. Commit `fa96afbd6286233f7f61992f7f443063acb4a118` passed its host tests and SH-4 app build. | The 1.0 integration retains `/ide/Games` and `/sd/Games` destinations; the maintainer has approved the combined app retest without a separate detailed reporting matrix. |
| FAT16/FAT32/exFAT | The 0.9.1 build passed the host suite, Linux interoperability and SH-4 build with the FatFs R0.16 fixes. | Host images do not model SD/IDE timing or every card. |
| Code Veronica on serial SD | Reported boot through ISO Loader. | Do not infer every preset, CDDA or VMU-emulation combination works. |
| Bust-A-Move 4 on serial SD | Reported black screen after executable CRC verification and handoff with standalone loader 0.9.2. | Known compatibility limitation; investigation was deferred. |
| IDE/CF with GD-ROM retained | Paths and filesystem support are included. | Physical coexistence, sustained ripping and game loading await results on that hardware. |
| Bootloader 3.2 recovery display | Host checks cover the RAM font atlas/upload, TV-safe geometry, empty inventory and insert/rescan/boot sequence. | On 2026-09-16 the maintainer reported: "New bootloader works great!" Detailed Start-held and failed-core checks were not individually reported. |
| K-UI 3.3 disc startup | The maintainer confirms that the `87c47a5` startup-fix disc reaches the connect-SD menu and boots K-UI after SD insertion. The complete SH-4 build, embedded early-init instructions and disc integrity were checked. | Keep the working disc for app testing. Other bootloader modes are not covered by this report. Emulation cannot reproduce the original freeze; no exact regression-causing commit is established. See the [artifact checks and regression trace](bootloader-startup-check.md). The sharper badge includes `github.com/TPMJB`. |
| Application bug batch | The latest maintainer retest reports the apps/navigation working after the File Manager Lua cleanup/binding fix, reload/directory fixes and shared pointer handling. | The earlier Speedtest mismatch received detailed diagnostics; its original cause was not established. Detailed app-by-app results were not supplied with the final approval. [Earlier investigation](1.0-test-followups.md). |
| Launcher / GD Ripper music | Five completed-rip resume/CRC checks with music off and five with music on succeeded. In the music-on checks, verification unloaded roughly 0.91 MiB and sampled available memory remained over 9 MiB. Host checks cover lifecycle, deferred storage and blocked-I/O controls. | These were completed-track resume/CRC checks, not ten fresh disc dumps or proof that music cannot contribute to another failure. No measured SD throughput guarantee. |
| Settings and GD Play | Visible active-tab/navigation and disc polling fixes are included from 0.9. | Save/reboot and lid/disc transitions were not individually itemized in the final general app report. |
| BIOS Flasher / Region Changer | Host failure-path tests and SH-4 compilation; physical Region Changer reads are implemented. | Flash writes require compatible hardware and a recovery path; comprehensive hardware validation is not claimed. |
| VMU Manager | Backup/copy/restore behavior has host tests and compiled modules. | Record actual VMU backup/restore and peripheral results. |
| NAOMI utilities | Existing Arcade, Cart Ripper and MIE JVS apps remain included. | Cart Ripper and MIE JVS need relevant arcade hardware; no new hardware validation is claimed. |

## Intermittent disc-dumping crash

A complete-machine crash was reported while dumping MDK2; no exception photo
was captured for that occurrence. The earlier VMU photograph shows an
`EXC_DATA_ADDRESS_READ` exception, but that does not identify the later ripper
failure. The rip and memory logs do not establish ordinary memory exhaustion.
Ten subsequent completed-rip resume/CRC checks succeeded. Preserve `rip.log`,
`kui-memory.log` and a full exception photo on recurrence; include disc revision,
storage/filesystem, exact build and music state. This issue remains open in 1.0.

Build evidence: [0.9.1 complete build](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/34975888753)
and [GD Ripper 2.2.2](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35003894677).

The startup diagnosis is based on the pinned KOS source:
[`arch_main()`](https://github.com/DC-SWAT/KallistiOS/blob/a78fa2a2761360d96b66ba913e447812d5f2b889/kernel/arch/dreamcast/kernel/init.c)
calls `KOS_INIT_EARLY` before clearing BSS, while
[`g1_ata_select_device()`](https://github.com/DC-SWAT/KallistiOS/blob/a78fa2a2761360d96b66ba913e447812d5f2b889/kernel/arch/dreamcast/hardware/g1ata.c) reads its
zero-initialized `dev_selected` cache and can call IRQ/thread helpers. The early
hooks now select the retail GD-ROM register directly without KOS runtime calls;
the devkit/NAOMI guard is retained. Host checks compile the actual hooks, reject
runtime/global-data dependencies, and verify the register access against a mapped
memory page. These checks do not emulate drive timing; the successful console
recovery-menu and SD-boot result is recorded separately above.

For a useful compatibility report, include the K-UI build/source commit, game
revision, SD/IDE device, FAT32/exFAT, loader settings, last visible message, and
whether the game merely reached its title screen or was played. Include
`last-launch.txt` when available. Never upload a commercial game image.
