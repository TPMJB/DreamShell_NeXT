# VMU Manager 2.0.0

Merge the update's `DS` folder onto the card used by DreamShell NeXT 1.0.
Replace the VMU Manager files and included font. Launch VMU Manager and check
that the top-right version reads **NeXT / 2.0**. The update uses the existing
DreamShell core and boot disc.

## Controls and workflow

- Choose one of the labeled A1–D2 VMU slots. Empty slots cannot be selected.
- Choose SD, IDE, PC, disc, or another VMU as the other location. The two lists
  show the current VMU and the other location; the lower panel shows the
  highlighted save's icon, filename, block count, and descriptions.
- D-pad or stick moves through saves and controls. Left/right moves between
  the lists. At the end of a list, down reaches the action buttons. A selects
  a save or opens a folder. **X / Copy** copies the selected save to the other
  side; its button states the direction. Browsing/selecting alone does not copy.
- **Backup VMU** writes a complete 128 KiB image to the displayed writable
  folder, named `VMU_A1_001.vmd` (with the selected slot and a free number).
- **Y / More actions** contains Delete, New folder, and Format. B returns to
  the previous page or goes up in the other location. **Location** changes the
  device; **VMUs** selects a different source card.
- Selecting a VMD/VMU image and choosing **Open VMU image** offers **Restore**,
  **Browse image**, or **Cancel**. Browsing an image does not restore it. Full
  restore requires a 128 KiB image and identifies the VMU being replaced.
- Keyboard arrows/Enter/Escape and X/Y provide the same navigation. Mouse
  clicks select rows and activate labeled buttons. Right-click selects a row;
  deletion is an explicit action. The existing on-screen keyboard edits folder
  names; Cancel returns without creating anything.

Overwrite, delete, and format require a fresh confirmation after the initiating
buttons are released. Opening a card no longer runs the old implicit directory
"ghost entry" repair. Explicit save operations run on one worker; IO failures
appear in the status line. Copies read the complete source before opening an
overwrite destination and check short writes and close failures. A failed
VMU write can still leave that save or card incomplete: the hardware does not
provide an atomic rollback operation.

## Validation and hardware checks

Host tests exercise source-read failures before overwrite, short writes, flush
and close failures, DCI conversion, protected paths, folder cancellation, and
confirmation input release. CI compiles and links the SH-4 module against the
current pinned DreamShell/KallistiOS build. The layout preview is rendered from
the native XML with illustrative save names; it is not a hardware screenshot.
Regenerate the six preview screens with
`python utils/preview_vmu.py --output /tmp/vmu-preview` (requires Pillow).

On a console, check navigation and scrolling, a VMU-to-SD backup, copying a
nonessential save back, canceling overwrite/delete/format, and selecting a
second VMU. Actual Maple timing and peripheral hot-plug behavior require this
hardware check.
