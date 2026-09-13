# DreamShell FatFs

This directory vendors FatFs R0.16 (ChaN, July 22, 2025) and the Dreamcast
adapter originally from DC-SWAT/FatFs commit
89f59cc62f99efcbaea000fd2de100f449b4608e. Copyright/license notices remain in
the individual source files. Vendoring keeps the filesystem, VFS adapter,
bootloader and release tests pinned together in this repository.

Upstream archive: https://elm-chan.org/fsw/ff/arc/ff16.zip
SHA-256: 99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e
Documentation: https://elm-chan.org/fsw/ff/00index_e.html

`ff.c`, `ffunicode.c` and `diskio.h` are unchanged upstream sources.
`ff.h` adds conditional transfer state to `FIL` for the ISO Loader only.
`ffconf.h`, `ffsystem.c`, `dc.c` and `dc_bdev.c` configure/adapt them to KOS.
The same upstream engine is compiled separately with the ISO Loader's small
configuration and its asynchronous read extension.
