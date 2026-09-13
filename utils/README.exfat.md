# DreamShell NeXT exFAT preview

This experimental build adds exFAT to bootloader **3.0**, the DreamShell core,
ISO Loader **0.9.0**, and the optional HollySH BIOS loader. It uses a pinned FatFs R0.16 engine. FAT16/FAT32
remain supported. GD Ripper uses the core's filesystem services; its existing
logging, CRC checkpoints and targeted recovery work through the same interface.

## Install and boot

1. Keep the working FAT32 card and boot disc as a fallback. Test with a spare
   card first; save any existing files elsewhere before formatting it.
2. Burn the included `DreamShell_bootloader_v3.0.cdi` as a disc image. The old
   2.9 bootloader cannot read an exFAT card just because its DS folder is updated.
3. Use a **512-byte-sector device**, either with an **MBR primary partition**
   formatted exFAT or formatted as a whole-device exFAT volume. GPT and devices
   at or above 2 TiB are not supported by this first build.
4. Copy the entire included `DS` folder to the card root. This update needs
   `DS_CORE.BIN`, the modules, and `DS/firmware/isoldr/`; replacing only GD Ripper
   is insufficient. Preserve your dumps in their existing folders.
5. Safely unmount the card, install it in the powered-off Dreamcast, and boot
   with the new CD. The boot screen identifies version 3.0 and exFAT support.

For Linux, `mkfs.exfat` and `fsck.exfat` are provided by **exfatprogs**. A
128 KiB cluster is a reasonable starting point; it is not a special DreamShell
format. Supported exFAT cluster sizes are handled by the filesystem, with a
32 KiB cap on the IDE DMA bounce buffer. The build does not automatically format
or repartition cards. Select MBR in your partitioning program if making a new
partition table; format the intended unmounted partition with exFAT.

The stock BIOS can start this boot CD and then load DreamShell from SD or IDE.
A custom boot BIOS is optional for booting without a CD. The release build also
produces BIOS payloads, but BIOS flashing and hardware modifications are not part
of the initial exFAT test. Keep the GD-ROM when choosing a future BIOS variant.

## Game storage and the future CF mod

Use **DreamShell ISO Loader** to select a supported game image, such as a `.gdi`,
on `/sd` or `/ide`. Its independent parser and synchronous/asynchronous reads
are included in this update, including fragmented files and UTF-8 paths.
The specialized raw streaming command requires the requested region to be
contiguous; it now refuses a request across fragments instead of reading unrelated
data. Normal synchronous and asynchronous reads follow fragmented files.
Game compatibility, CDDA, VMU writes and transfer timing still need real-console
tests. Serial SD remains limited by the serial connection.

This does not update GDEMU firmware. GDEMU replaces the optical drive and reads
its own SD card; it does not expose that card as ordinary writable DreamShell
storage. With a future G1-ATA/IDE mod, retain the GD-ROM as master and use a
compatible CF card/adapter configured as slave. The filesystem support is shared,
but coexistence, DMA and sustained GD-ROM-to-CF ripping need hardware testing.

## Limits and verification

- KOS's `fs_seek64`, `fs_tell64` and `fs_total64` hooks support exFAT files beyond
  4 GiB. Legacy 32-bit applications do not automatically gain large-file support.
  Legacy stat/seek operations reject overflow; directory-list size fields clamp
  at their signed 32-bit limit. ISO Loader rejects individual files above its
  signed 2 GiB offset range. Normal Dreamcast GDI track files fit that range.
- Game loaders grow by several KiB. A low-address preset that would overlap the
  game executable now stops with an error before launch. Select loader address
  `0x8ce00000` or reduce emulation features if you encounter that message.
- Filesystem paths use UTF-8. Fonts in individual applications may not contain
  every character that the filesystem can store.
- Mounting does not scan the whole free-space bitmap/FAT just to display a
  diagnostic. The IDE bounce buffer never scales up to a 16 MiB exFAT cluster.
- Write and flush errors must propagate to callers. exFAT is not journaled;
  existing ripper checkpoint, sync and recovery safeguards remain necessary.

Automated checks use Linux-formatted disk images and the production DreamShell
VFS adapter. They cover raw volumes and MBR, FAT16/FAT32 regressions, UTF-8,
append/reopen/remount, fragmented files, 64-bit offsets and injected flush errors.
The ISO Loader separately reads the resulting fragmented fixtures synchronously
and asynchronously. Linux `fsck.exfat -n` independently checks the written image.
The GD Ripper tests exercise real logger, CRC and recovery code on all three
filesystem types. These checks do not emulate physical SD, IDE or GD-ROM timing.

## First hardware checks

1. Boot from the new CD with the updated DS folder on FAT32, then on a spare
   exFAT card. Check automatic disc-title detection and application loading.
2. Rip a known-good disc into a new folder. Confirm `rip.log`, `rip.state`,
   checkpoints, completion and the expected TOSEC CRC match.
3. On a separate fresh test dump, press Stop, return to idle, restart the app,
   and resume. Check the final CRC and inspect the card with Linux afterward.
4. Test ISO Loader with a known-good game image. Try CDDA/VMU features separately
   so any problem can be tied to a specific operation.
5. Record timings on the same card and workload for FAT32/exFAT: boot, browsing,
   a large copy, ripping and checkpoint pauses. No speed improvement is claimed
   before that comparison.

Save the screen/error text and logs if anything fails. Do not deliberately cut
power during your first test. Automated fault tests cover injected write/sync
failures; real power-loss behavior is a separate validation step.

EXT4 remains a future filesystem port. This implementation leaves the existing
KOS block-device/VFS boundary intact so another filesystem can use the same SD
and IDE drivers.

Source references: [FatFs configuration](https://elm-chan.org/fsw/ff/doc/config.html),
[FatFs application notes](https://elm-chan.org/fsw/ff/doc/appnote.html),
[exfatprogs](https://github.com/exfatprogs/exfatprogs).
