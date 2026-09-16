# K-UI keyboard and directory picker

The complete NeXT 1.0 installation includes the QWERTY keyboard and modal input
handling, plus GD Ripper 2.2.2's directory picker and Games destinations. Follow
the [installation guide](../docs/installation.md) and copy the full DS folder
so the core, keyboard module and apps match.

## Keyboard

Select a text field to open the QWERTY editor. It shows the current text and an
insertion cursor. The alphabet and symbol pages include directory punctuation.

- D-pad/stick: move between keys; hold to repeat navigation.
- A: select the highlighted key. X: Backspace. Y: toggle Shift.
- `123 #+=` / `ABC`: change pages. Shift also changes the symbol page.
- `<` and `>`: move the insertion cursor. Space and `_` have dedicated keys.
- Done or START: accept changes. B: cancel and restore the original value.
- Mouse: click keys. A connected physical keyboard still types directly into
  fields; DreamShell hides/unloads the onscreen keyboard when one is attached.

Typing is limited to printable ASCII, matching DreamShell's existing text
fields. Field length limits still apply. Filename cleanup happens when the
ripper accepts a manually edited new folder name. Selecting an existing dump
preserves its actual name, including spaces.

The old keyboard was forwarding synthetic typing through two active input
handlers in GD Ripper. Input overlays now run before application handlers;
consumed events stop there. The ripper also consumes each event it forwards
to the screen, so it cannot be delivered twice. App startup enables default
input before calling `onopen`; the ripper can then disable controller mouse
emulation and take ownership without that choice being overwritten.

## Directory picker

Choose **Destination**, then SD card, IDE drive, or PC share. Open folders with
A or a click. The seven-row list stays inside the panel and provides Previous
page / Next page. Up goes to the parent; B goes up or cancels at the device
root. The current path and page count appear above/below the list.

**Use this folder** selects the parent destination. A new rip still gets its
own folder, initially named from the inserted disc. To resume or scan a saved
rip, open the directory containing its `rip.state`, then choose **Select this
dump**. Both the parent path and name are filled in for you. Cancel leaves your
previous selection unchanged. The device buttons open or create their Games
folder. Other browsing selects existing directories; the new disc-title folder
is created when you Start.

## Bounded FAT32 regression check

Use the same new build and exFAT-capable bootloader on an existing FAT32 card;
there is no need to reformat a working card. Use a known-good disc that already
matched the catalog on exFAT, and a fresh dump folder.

1. Boot from FAT32; open the launcher, ripper, keyboard and folder picker.
2. Start the rip. During track 3, select Stop once and wait for a paused/idle
   result. Start / Resume with the same disc and selected folder.
3. Let it finish. Check the final catalog result and retain `rip.log`,
   `rip.state`, `rip.complete`, `verify.log`, and track `.crc` checkpoints.
4. Power down, then independently hash the resulting track files on the PC.
   Compare them with the catalog and the known-good exFAT dump.

One known-good full rip with a clean Stop/Resume covers the useful first
regression pass. A full storage read-back is optional if the PC hashes and
stream result already agree. If the console and PC disagree, preserve the
logs/checkpoints/sector maps before running another scan or repair.

## Validation and remaining hardware check

Host tests execute the production input dispatcher and keyboard with a GUI
shim, and the production ripper with a mock optical drive and POSIX VFS.
They exercise single input delivery, modal isolation, cursor edits, capacity,
cancel/accept, held navigation, mouse clicks, unload/reload and missing font
cleanup; directory tests exercise pagination, device failure, cancellation,
file filtering and existing-dump selection. Actions additionally checks
FAT32/exFAT interoperability and performs the full SH-4 release build.

Real controller responsiveness, font rendering and both card formats still
need the console check above.
