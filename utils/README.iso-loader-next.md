# DreamShell NeXT ISO Loader 2.0.3
TPMJB · https://github.com/TPMJB/DreamShell_NeXT

This update contains the ISO Loader app, Games Menu launch and thumbnail fixes, ISOFS and ISO Loader modules, and standalone 0.9.1 firmware. It is intended for the current DreamShell NeXT build. Keep the modules and firmware together.

## Install
1. Back up the existing DS/apps/iso_loader, DS/apps/games_menu/app.xml, DS/apps/games_menu/modules/app_games_menu.klf, DS/modules/isoldr.klf, DS/modules/isofs.klf and DS/firmware/isoldr directories/files.
2. Extract this update and merge its DS folder into the DS folder on your card or drive. Replace the supplied files. Your presets and VMU saves are not included in the update.
3. Restart DreamShell. The app should show v2.0.3; the standalone loader should show v0.9.1.
4. No new boot disc is required for this app/module update. The firmware package uses ELF files; sd.bin is not required.

## 2.0.3 console feedback
Code Veronica has been confirmed to launch through ISO Loader on the user's console. This update addresses the remaining D-pad and Games Menu differences; Games Menu booting still needs a console check.

ISO Loader now uses spatial D-pad navigation. Up/Down browse rows, Left/Right move between controls, X jumps to the top bar and Y jumps to the action bar. A selects or opens the highlighted item; moving focus alone does not inspect or launch it. The analog cursor remains enabled. Hidden pages and off-screen controls are excluded. Error dialogs continue to consume the full click.

Games Menu automatic defaults now use the same 8ce00000 loader address as ISO Loader (WinCE retains its minimum-address requirement). The title buffer, configuration read bounds/terminator, patch values, eight-digit address normalization, Auto device selection and alternate executable path are fixed. SD DMA/alternate reads are disabled. Games now checks the executable before handoff and writes DS/apps/games_menu/last-launch.txt with the game, settings and executable CRC. A Games-specific saved preset still takes priority over an ISO Loader preset; existing preset files are kept.

In Games settings, choose **Extract disc thumbnails** (formerly “Scan missing covers”). This reads 0GDTEX.PVR from inside each game image, creates the covers directory if necessary, and generates the menu-size copies. Missing/failed thumbnails can be retried. The ripper preserves the embedded disc files in the tracks; it does not export retail box art. Games without an embedded thumbnail need an external cover. Cover files live under DS/apps/games_menu/covers, with names derived from the game/folder name.

The **TPMJB NeXT** theme adds navy panels and lighter text. Select it in Games' theme settings; existing saved colors are preserved.

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
