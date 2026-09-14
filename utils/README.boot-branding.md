# DreamShell NeXT — TPMJB boot disc

The corner badge beside the SEGA startup screen now shows DreamShell NeXT,
TPMJB on the right, and **github.com/TPMJB** underneath. The navy and cyan
artwork matches the NeXT interface.

## Install

1. Extract `DreamShell-NeXT-TPMJB-bootloader-v3.0.cdi` from this ZIP.
2. Burn the CDI **as a disc image** onto a new CD-R using the same working
   image-burning method as your current DreamShell boot disc.
3. Boot the Dreamcast with the new CD-R and your existing SD card.

The boot logo lives on the CD. Copying this ZIP or its artwork to SD does not
replace it. This is the NeXT exFAT-capable **3.0** bootloader with new disc
branding; it loads the DreamShell installation already on your SD/IDE device.
Keep your installed VMU Manager update. No SD-folder replacement or BIOS
flashing is part of this update.

`boot-disc-badge.png` is the actual palette-limited badge preview; the
background is transparent on the boot screen. This package has been built
and checked in CI, but the new CD-R still needs a console boot check.

## Artwork and source

Edit `resources/boot-disc-badge.svg`, then regenerate using:

```sh
python3 utils/build_boot_disc_branding.py --makeip /path/to/kos/utils/makeip/makeip
```

This host step requires CairoSVG and Pillow. It uses KallistiOS's `makeip`
encoder, enforces the MR image size limit, and replaces only the old logo
region of the existing `resources/IP.BIN`. Normal builds use the committed
assets and do not need an image renderer. The package check reads the logo
back from the generated CDI and verifies the surrounding bootstrap bytes.

Branding and NeXT enhancements: **TPMJB**, https://github.com/TPMJB.
Built on SWAT's DreamShell, KallistiOS, FatFs, and their contributors' work.
Original notices are included in `LICENSE` and `NOTICE` and remain in the
source tree: https://github.com/TPMJB/DreamShell_NeXT.
