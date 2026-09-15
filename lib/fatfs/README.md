# DreamShell FatFs

This directory vendors FatFs R0.16 with official patches 1 and 2 (ChaN) and the
Dreamcast adapter originally from DC-SWAT/FatFs commit
89f59cc62f99efcbaea000fd2de100f449b4608e. Copyright/license notices remain in
the individual source files. Vendoring keeps the filesystem, VFS adapter,
bootloader and release tests pinned together in this repository.

Upstream archive: https://elm-chan.org/fsw/ff/arc/ff16.zip
SHA-256: 99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e
Documentation: https://elm-chan.org/fsw/ff/00index_e.html

`ff.c` includes the official patches below, applied in order to R0.16.
`ffunicode.c` and `diskio.h` are unchanged upstream sources.
`ff.h` adds conditional transfer state to `FIL` for the ISO Loader only.
`ffconf.h`, `ffsystem.c`, `dc.c` and `dc_bdev.c` configure/adapt them to KOS.
The same upstream engine is compiled separately with the ISO Loader's small
configuration and its asynchronous read extension.

## Official maintenance patches

The unmodified patch files are retained in `patches/` for reproducibility.

- [Patch 1](https://elm-chan.org/fsw/ff/patch/ff16p1.diff), September 13, 2025:
  correct small FAT volume formatting and reject unsupported cluster counts.
  SHA-256: `7996ddc3135f3d534a8153f7d75d587030e5c8551b003c3b7ee823ffb7fc64bf`.
- [Patch 2](https://elm-chan.org/fsw/ff/patch/ff16p2.diff), July 10, 2026:
  reject invalid exFAT cluster counts and oversized FAT tables; bound exFAT
  label reads to the eleven UTF-16 code units in the label field.
  SHA-256: `5cd39f1fc299f0f1dec9fa1ce544dc0bc048b2f6eb3b8161476ed20f9cf1e290`.

These shared engine fixes reach the rebuilt core, bootloader and ISO Loader.
The NeXT VFS adapter, flush-error handling and ISO Loader asynchronous read
extension are unchanged. Host regression cases in
`utils/tests/test_fatfs_validation.py` exercise the affected APIs against
memory-backed FAT/exFAT media using the production configuration.
