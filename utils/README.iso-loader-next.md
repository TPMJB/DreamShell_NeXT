# DreamShell NeXT ISO Loader 2.0.5 + Games 1.0.1
TPMJB · https://github.com/TPMJB/DreamShell_NeXT

This update contains the ISO Loader app, Games Menu launch and thumbnail fixes, ISOFS and ISO Loader modules, and standalone 0.9.2 firmware. It is intended for the current DreamShell NeXT build. Keep the modules and firmware together.

## Install
1. Back up the existing DS/apps/iso_loader, DS/apps/games_menu/app.xml, DS/apps/games_menu/modules/app_games_menu.klf, DS/modules/isoldr.klf, DS/modules/isofs.klf and DS/firmware/isoldr directories/files.
2. Extract this update and merge its DS folder into the DS folder on your card or drive. Replace the supplied files. Your presets and VMU saves are not included in the update.
3. Restart DreamShell. ISO Loader should show v2.0.5; Games is v1.0.1; the standalone loader should show v0.9.2.
4. No new boot disc is required for this app/module update. The firmware package uses ELF files; sd.bin is not required.

## Firmware 0.9.2: WinCE SD physical DMA destinations

The Bust-A-Move 4 console video reaches **Executable CRC matched**, **Preparing game hardware**, and **Executing** before going black. Its Baseline report confirms loader address `8c000100`, CDDA/IRQ/VMU off, and executable CRC `f6902800`. Initial executable reading and verification succeeded; the failure occurs at or after handoff.

SD emulates GD-ROM DMA reads using CPU copies. GD DMA requests supply physical destination addresses, but the loader passed those directly to the CPU read path. Under WinCE's MMU, a physical address in P0 is interpreted as a virtual address and can fault or access a different page. Firmware 0.9.2 converts normal WinCE SD DMA read destinations to the untranslated P1 alias; the existing transfer code writes back and invalidates that range. WinCE PIO pointers retain their virtual addresses. Other executable types and IDE/CD paths retain their existing behavior. The GD DMA buffer convention is documented by the [KallistiOS implementation](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/cdrom.c).

The regression test submits requests through production `gdcReqCmd` and the production transfer functions, covering synchronous, chunked and pseudo-async reads, cache-purge destinations, virtual PIO pointers, existing cached/uncached aliases, Katana and IDE behavior. It does not emulate a Dreamcast or prove Bust-A-Move 4 uses the affected command.

For the console check, keep the same Bust-A-Move 4 image and **Baseline** settings, press **Play**, and confirm the standalone banner says **0.9.2**. Check whether the game reaches its title screen; also recheck the known-working Code Veronica. Both launch apps use the updated firmware. If Bust-A-Move 4 still fails after **Executing**, that remains a runtime compatibility failure needing deeper diagnostics. The separate SD restriction on WinCE DMA *streaming* remains; this fix covers ordinary DMA sector reads.

The download contains `DS` directly, with no nested ZIP. App versions remain ISO Loader **2.0.5** and Games **1.0.1**; only the standalone firmware advances to **0.9.2**.

## Earlier 2.0.5 / Games 1.0.1: WinCE baseline and launch reports

Bust-A-Move 4 is identified as WinCE in the bundled presets. ISO Loader's Baseline button previously forced every disc to loader address `8ce00000`, overriding the WinCE address `8c000100` used by automatic defaults and Games. Baseline now inspects the executable again, bypassing any saved OS override, and selects `8c000100` for WinCE. Other executable types retain `8ce00000`. The selected address is shown in the status line. This fixes the diagnostic profile; it does not establish that Bust-A-Move 4 boots on serial SD.

Games now updates `DS/apps/games_menu/last-launch.txt` with the actual error if inspection fails or the loader returns before handoff. Previously the file could still say the executable check passed after a firmware loading failure. It now includes the executable filename and size, heap and low-level setting. Games' launch settings are unchanged.

For the next console check, select Bust-A-Move 4 in ISO Loader, choose **Baseline**, then **Check**, then **Play**. Confirm the baseline status shows **WinCE** and **8c000100**. If it still fails, record the last visible loader message and copy `DS/apps/iso_loader/last-launch.txt`. Also attempt it from Games and copy `DS/apps/games_menu/last-launch.txt`. Copy these before launching another title, which replaces the report. These files describe preparation and returned failures; they cannot record a crash after DreamShell has handed control to the game.

Versions: ISO Loader app **2.0.5**, Games **1.0.1**. Shared ISO Loader **0.9.3**, ISOFS **1.8.1**, and standalone firmware **0.9.1** are unchanged. No game-specific compatibility patch is claimed; a real-console test is still required.

## Earlier 2.0.4 / Games 1.0.0: controller and library update

Games opens with a new TPMJB layout immediately. It has an eight-row list with a large artwork preview, a compact two-column view, and a six-game gallery with titles. The action bar stays visible in every view. Existing Games launch defaults, paths and saved presets are retained.

- **Games:** D-pad browses; A plays; X cycles views; Y opens game setup; L/R changes pages. **Start** focuses the action bar. Left/Right chooses an action; A activates it; B or Up/Down returns to the games. The analog pointer and a mouse can select the full visible buttons and game cells.
- **Scan artwork:** click the button, or press Start, Right, Right, A. Every scan visits all discovered games across categories. Existing artwork is kept; missing artwork is extracted from the embedded `0GDTEX.PVR` and resized for the menu. Progress and a completion report stay visible. B requests cancellation after the current disc; A/B dismisses the result. A later scan retries unavailable games. “Unavailable” includes absent thumbnails and read/write failures; this is not a box-art download service.
- **ISO Loader:** A directly activates the visible folder/item or button on release, including rows reached through D-pad scrolling. It no longer relies on mouse-click emulation. Adjacent duplicate mouse events are discarded; physical mouse input is retained. X jumps to the top bar; Y jumps to the bottom actions; Start launches.

Versions: ISO Loader app **2.0.4**, Games **1.0.0**, shared ISO Loader module **0.9.3**, ISOFS **1.8.1**, standalone firmware **0.9.1**. Merge the supplied DS folder and reboot. Existing game paths, covers, configuration, presets and saves are not replaced. The ZIP includes Games' UI module, font and bundled images. No new boot disc or sd.bin is required.

The included previews illustrate the production layout; they are not console screenshots. Host tests and SH-4 builds cannot confirm behavior on real hardware. The main checks after installing are A opening folders in ISO Loader, Start/A accessing Games actions, scan results showing new artwork, and launching the same known-working game.

## Earlier 2.0.3 launch fixes (retained)

Code Veronica has been confirmed to launch through ISO Loader on the user's console. This update addresses the remaining D-pad and Games Menu differences; Games Menu booting still needs a console check.

ISO Loader now uses spatial D-pad navigation. Up/Down browse rows, Left/Right move between controls, X jumps to the top bar and Y jumps to the action bar. A selects or opens the highlighted item; moving focus alone does not inspect or launch it. The analog cursor remains enabled. Hidden pages and off-screen controls are excluded. Error dialogs continue to consume the full click.

Games Menu automatic defaults now use the same 8ce00000 loader address as ISO Loader (WinCE retains its minimum-address requirement). The title buffer, configuration read bounds/terminator, patch values, eight-digit address normalization, Auto device selection and alternate executable path are fixed. SD DMA/alternate reads are disabled. Games now checks the executable before handoff and writes DS/apps/games_menu/last-launch.txt with the game, settings and executable CRC. A Games-specific saved preset still takes priority over an ISO Loader preset; existing preset files are kept.

In Games settings, choose **Extract disc thumbnails** (formerly “Scan missing covers”). This reads 0GDTEX.PVR from inside each game image, creates the covers directory if necessary, and generates the menu-size copies. Missing/failed thumbnails can be retried. The ripper preserves the embedded disc files in the tracks; it does not export retail box art. Games without an embedded thumbnail need an external cover. Cover files live under DS/apps/games_menu/covers, with names derived from the game/folder name.

Games now uses the new branded layout by default. Theme settings continue to control the settings dialogs.

Versions: ISO Loader app 2.0.3; ISO Loader module 0.9.3; ISOFS 1.8.1; Games Menu 0.9.3; standalone firmware 0.9.1. The ZIP includes Games Menu's XML/module, preserving its configuration, covers and presets.

The 2.0.2 GDI parser change is retained, with the supplied Evolution 2 descriptor as a regression fixture.

## Evolution 2: first console test
Select the CRC-verified dump, choose **Baseline**, then **Check**. Baseline is an unsaved diagnostic profile: Direct boot, loader at 8ce00000, async 8, DMA off, CDDA and VMU emulation off, visible loader messages and executable verification on. It bypasses the saved game preset without deleting it.

Check verifies the GDI descriptor/track set where applicable and reads the executable in DreamShell. It does not calculate a full-disc CRC or certify compatibility. Choose **Play** to read the executable again in the standalone loader and compare its CRC before patches and execution.

Watch the final line:
- A specific image/track/preset/loader error: fix the reported item before retrying.
- “Executable read failed” or “Executable CRC mismatch”: the standalone read did not produce the expected data; the loader stops.
- “Executable CRC matched” followed by a black screen: the two reads agreed. Game compatibility, patches, memory use and the later handoff still need investigation.

Errors at the executable-loading stage remain visible for three seconds before the loader attempts to return to DreamShell. Photograph the final screen if the console still goes black. **Details** opens the last preparation report in the console. Its text is also saved as DS/apps/iso_loader/last-launch.txt when the app directory is writable. The previous completed report is retained as last-launch.previous.txt. It records preparation, not proof that the game ran.

Use **Game preset** to restore the saved or bundled game settings. You can enable CDDA after obtaining a baseline boot; this profile is intentionally quiet. Settings and More retain the existing advanced controls. **Save as preset** remains opt-in.

## Controls and browsing
- Analog stick: move the cursor on every page. A/Enter: click the control under it. Start/Space: play.
- D-pad: move focus in the pressed direction. Up/down over a file list browse and scroll its rows. A selects/opens the highlighted row.
- X: focus the top bar. Y: focus the bottom action bar on the Games page. Left/right traverse either bar. The stick remains available throughout.
- B/right-click/Escape: go up a folder, or return from Settings to Games.
- A/B/Enter/Escape: dismiss an error dialog. Other controls are blocked while the dialog is open.
- Mouse and keyboard remain available. Use the Check and Settings buttons for those actions.
- Media previews are off by default. Enable them in More and reselect a game.
- Play, Check, Baseline, Game preset, Details and Up also have explicit buttons.

## Storage and limits
FAT32 and exFAT were already enabled in the FAT loader. This update does not require reformatting a working card. DreamShell's serial-port SD adapter has its own SD loader; SD DMA and the IDE-specific alternate-read setting are forced off.

Raw 2352-byte GDI data tracks are supported and unpacked into 2048-byte data sectors. The standalone GDI reader still expects the standard trackNN.bin / trackNN.iso data names and trackNN.raw / trackNN.wav audio names, with zero descriptor offsets. Unsupported naming or missing/truncated tracks now produces an error. Keep the ripper-generated descriptor and tracks together. Compressed-image compatibility and game-specific requirements remain format dependent.

Executable verification covers Dreamcast disc images. NAOMI/Bleem launches keep their existing paths; the Check action reports that executable CRC verification is unavailable for them. Baseline is for Dreamcast disc images.

Older low-memory presets may no longer fit the complete modern loader, including its BSS. They are rejected with an explanation instead of skipping an assumed 32 KiB of executable data. ELF firmware is preferred, with bounded header/segment/relocation reads. Legacy BIN firmware remains a fallback only if the ELF is absent.

## Validation
The build runs the repository's storage/ripper tests, new host tests that compile the production raw-sector reader with short-read/seek/EOF fault injection and buffer canaries, CRC/layout/GDI/ELF-boundary tests, and the native XML/export contract check. It also tests Games Menu cleanup for inspection errors and returned launch failures, checks the SH-4 parser object has no runtime imports, and cross-compiles the app/modules and standalone firmware.

These checks do not replace a Dreamcast hardware boot test. Evolution 2 success is not claimed by this update.
