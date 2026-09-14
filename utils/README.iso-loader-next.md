# DreamShell NeXT ISO Loader 2.0.2
TPMJB · https://github.com/TPMJB/DreamShell_NeXT

This update contains the ISO Loader app, a Games Menu launch-error fix, ISOFS and ISO Loader modules, and standalone 0.9.1 firmware. It is intended for the current DreamShell NeXT build. Keep the modules and firmware together.

## Install
1. Back up the existing DS/apps/iso_loader, DS/apps/games_menu/app.xml, DS/apps/games_menu/modules/app_games_menu.klf, DS/modules/isoldr.klf, DS/modules/isofs.klf and DS/firmware/isoldr directories/files.
2. Extract this update and merge its DS folder into the DS folder on your card or drive. Replace the supplied files. Your presets and VMU saves are not included in the update.
3. Restart DreamShell. The app should show v2.0.2; the standalone loader should show v0.9.1.
4. No new boot disc is required for this app/module update. The firmware package uses ELF files; sd.bin is not required.

## 2.0.2 follow-up
The supplied Evolution 2 descriptor is valid and is now a regression fixture. The GDI parser uses explicit ASCII character checks and bounded 32-bit number parsing instead of imported newlib character tables. Both ISO Loader preflight and ISOFS mounting use it. This removes a possible source of console-only parsing differences; the original on-console rejection has not been conclusively reproduced.

Games Menu uses the same ISO Loader module. If image inspection or launch preparation fails, it now displays the loader's error after returning to the main menu. A returned isoldr_exec call is a failure, not a successful handoff: the app keeps DreamShell running and frees its menu resources exactly once.

Versions: ISO Loader app 2.0.2; ISO Loader module 0.9.3; ISOFS 1.8.1; Games Menu 0.9.2; standalone firmware 0.9.1. This ZIP includes Games Menu's XML/module only, preserving its images, configuration and presets.

The 2.0.1 cursor fix is retained: analog cursor and native D-pad focus work on every page, including the top bars. Error dialogs consume the full click and hide the page behind them to prevent asynchronous redraws covering their text. GDI errors show the rejected descriptor line and distinguish parsing, numbering and sector-order problems.

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
- D-pad up/down: move focus through all visible controls, including the device bar and top tabs. Left/right: first/last control.
- With the cursor over the game list, hold X or Y and use D-pad up/down to select rows, left/right to move a page, or the analog stick to scroll. Release X/Y to resume the cursor.
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
