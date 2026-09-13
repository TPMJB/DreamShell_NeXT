# DreamShell NeXT launcher preview

Launch App 2.0 replaces the animated app grid with a controller-operated list and
an app details pane. It uses DreamShell's existing Tsunami renderer at 640x480.
Installed apps and saved Lua/DSC shortcuts remain discoverable. The original
Main application is available in the list as **Classic launcher**.

This preview is based on exFAT commit `6958fdc`. It does not change the
filesystem, bootloader, ISO Loader or GD Ripper behavior. The core adds the
`pvr_wait_render_done` export so the launcher can wait until the GPU has finished
sampling textures before freeing them. The separate `exfat` release
remains available while the new launcher is tested on a console.

## Install

If you already have that working exFAT preview:

1. Finish or stop the current rip, wait for the application to return to idle,
   and power down before removing the card.
2. Extract **DreamShell-launcher-update.zip**. Merge its `DS` folder onto the
   card, overwriting matching files. This replaces `DS/apps/launch_app/` and
   installs `DS/fonts/txf/helvetica.txf` and the accompanying `DS/DS_CORE.BIN`.
   Copy the core too: the new launcher needs its graphics synchronization export.
   Preserve your existing shortcuts.
3. Safely unmount the card and boot with your existing bootloader 3.0 CD.
   There is no need to burn another disc for this launcher update.

The full **DreamShell-launcher.zip** includes the updated core and the existing exFAT boot files
for a complete installation. Follow its `exfat-guide.md` if upgrading from an
older core. The small launcher update assumes the current exFAT preview core.

If your home screen is configured to use Main, open **Launch App**, or select it
as the main application in Settings. To revert the UI, restore the previous
`DS/apps/launch_app/` directory from DreamShell-exfat.zip. Old grid `config.lua`
files left by a merge are ignored by the new launcher.

## Controls

| Input | Action |
|---|---|
| D-pad up/down or analog stick | Select an app; hold to repeat |
| D-pad left/right or L/R | Move by seven apps |
| A | Open the selected app |
| B | Cancel a dialog; stay at home when already at home |
| Start | Open Settings |
| X | Request deletion, with Cancel selected initially |
| Mouse | Hover to select; click a row or Open app to launch |
| Keyboard | Arrows/Page Up/Page Down, Enter, Escape, F1 Settings, Delete |

Selection is remembered when returning from an app, including when DreamShell
unloads the launcher to reclaim memory. It lasts for the current boot session
and does not write a preference file to the card. Controller movement never
moves the mouse pointer.

## App details and artwork

`DS/apps/launch_app/catalog.xml` supplies short descriptions, display titles,
categories and list ordering for the bundled apps. The `app` attribute matches
the installed application's original XML name. Unknown apps remain selectable
with a generic description; missing artwork never removes an app from the list.

For optional screenshots, add `images/preview.png` inside the corresponding
application folder. A catalog entry may instead specify a `preview` path,
relative to `DS/apps/launch_app/`. For example:

```xml
<entry app="GD Ripper" title="GD Ripper" category="Disc tools" order="10"
       description="Rip discs, check CRCs, and recover damaged sectors."
       preview="../gd_ripper/images/preview.png" />
```

Use small PNG or PVR images, no more than 512 pixels on either side. Prefer
power-of-two dimensions such as 256x256 or 512x256 for native textures. Keep text
in the description, where it stays readable on a television. This build falls
back to the existing app icon when no preview is supplied; it does not bundle
new application screenshots or video playback.

Preview files are read after selection has settled for 220 ms, outside the
render callback. Two decoded previews are retained; at most one additional
image is being loaded. Row icons are limited to 64 pixels per side. Oversized
headers are rejected before decoding. Eviction and application shutdown wait
for the GPU before releasing textures.

## Validation and console checks

The host harness compiles the production launcher C and parses the production
XML using DreamShell's bundled Mini-XML. It exercises navigation, held-input
repeat, mouse click cancellation, deletion confirmation, remembered selection,
text fitting with the shipped font, missing images, preview caching and stale
loads. The release workflow also runs the existing storage and ripper tests,
then compiles the full SH-4 release.

Host checks do not emulate controller timing, the PowerVR renderer or serial
SD latency. On the console, check:

1. Boot and move through every page. Text and selection should stay within the
   panels, including long app and shortcut names.
2. Hold up/down, then release. Selection should stop promptly. Use A to open
   GD Ripper, return home, and confirm it is still selected.
3. Open Settings with Start, then return. Press B at home; nothing is deleted.
   If checking the X dialog, choose Cancel.
4. Open GD Ripper and File Manager, return home several times, and watch for
   missing textures, crashes or increasing pauses.
5. Optionally add a preview image and move quickly between apps; the detail
   pane should always belong to the currently selected app.

Continue the exFAT rip/CRC and Stop/Resume checks separately so an error can be
associated with storage or launcher behavior.

For developers, run `python3 -m unittest discover -s utils/tests -p test_launcher.py -v`.
With Pillow installed, `python3 utils/preview_launcher.py preview.png` renders a
layout preview from the actual C scene, XML and TXF font. An optional final
integer selects another app. This is a layout rendering, not a console screenshot.
