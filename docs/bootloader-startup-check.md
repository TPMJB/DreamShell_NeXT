# Bootloader disc checks — 2026-09-16

The reported console freeze on the SEGA license screen remains unresolved.
The maintainer identified the original K-UI bootloader CDI as the burned image,
then reported that the old bootloader disc also failed and that startup still
hung with the SD card removed. In the follow-up comparison, the old disc still
hung with the entire SD adapter disconnected; a known retail game disc worked.
Retail-disc success does not establish CD-R readability.

## Actual build artifacts checked

| Image | Source | Complete build | CDI SHA-256 |
| --- | --- | --- | --- |
| Bootloader 3.2 | `0152a59156f79898471867e8bdab0557c0dd1da5` | [35093040958](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35093040958) | `028e9886a59e7e45b4e415a38f01903ce3f1eece2324545e9ce8b5e22b9ff9ae` |
| Original K-UI 3.3 | `826140e7461c3854a188216f04c8f3a2d73d6e07` | [35134284392](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35134284392) | `54b1fac2436963f49683d0610066d96ad76c83113862ca3b916ceefa26e6e05a` |
| K-UI 3.3 with early-init fix | `87c47a5746e16e15eac336c209ab5e77b43e5cf5` | [35138681162](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35138681162) | `d641b815efcac2fc1754aa0310bb539656bb2527535fcb2fc3f5126da6e73ef2` |

All three contain the expected `1DS_BOOT.BIN` in the ISO9660 root. The bootstrap,
filesystem metadata and entire boot executable occupy the first 195 data-session
sectors; their Mode-2/Form-1 EDC and both P/Q ECC checks pass. ECC was checked with
libchdr's independent `ecc_verify()` implementation, not cdi4dc's encoder.

Outside the reserved logo region, the old/new bootstrap differences are limited
to the version and description fields. The executable bootstrap code is unchanged.
All three MR logos decode to their declared dimensions and fit the reserved area.
The latest badge is 5,220 bytes; the logo allowance is 8,192 bytes.

SH-4 disassembly of the latest CDI's descrambled executable confirms that the
early hook writes the master-device register directly. It no longer calls the
KOS ATA driver before BSS initialization. The full build and host CI for that
commit both succeeded. This verifies inclusion of the fix, not its effectiveness
against the console report.

## Emulation result and limit

Each unmodified CDI was run in Flycast source `215a5af1650c49b57fa12430d723fddb34b009cc`
using the retail 1.01d BIOS already present in the project, HLE BIOS disabled,
fresh emulator state, and no SD image. A local headless harness used Flycast's
null renderer, enabled serial output, and exposed RAM/VRAM/context for inspection;
CPU execution, BIOS, disc contents and drive emulation were not patched.

All three reached the KallistiOS banner, controller initialization and the
bootloader's video setup/device detection. The SEGA-screen freeze was not
reproduced. All then waited in the SCI receive loop during the fallback SD probe.
Flycast's SCI registers are passive storage and do not emulate the synchronous
receive operation, so these runs cannot establish whether the real console would
reach the recovery menu. This emulator wait is not evidence of the cause of the
reported console freeze.

## Logo-only diagnostic control

The old 3.2 disc also contains our custom DreamShell NeXT badge. It is therefore
not an untouched-artwork control for the maintainer's question about the K-UI
badge. Comparing `resources/IP.BIN` with its version before branding commit
`544a633` confirms that all branding changes in the tracked bootstrap are inside
the reserved logo region. The license/bootstrap program was not overwritten.
The custom badges use the run encoding implemented by KOS makeip.

`K-UI_bootloader_v3.3_stock-badge-control_826140e.cdi` restores the historical
DreamShell logo region in the original K-UI build `826140e`, the build supplied
for the reported failing disc. It intentionally does not incorporate the newer
early-init fix or change the boot executable: doing so would confound the logo
comparison. This is a diagnostic control, not a replacement release or a
confirmed fix.

| Property | Verified result |
| --- | --- |
| Control CDI SHA-256 | `b65e8633019a002a340489ba75561f35d4fd82c93378e8c588d31d6546a3acc8` |
| Control CDI size | 2,224,787 bytes, identical to the source image size |
| Historical bootstrap source | `git show 544a633^:resources/IP.BIN` |
| Historical bootstrap SHA-256 | `414af0e9af32c96f51e229b5fad13d724c948a59e110ece53f5da0fa1ffdd47f` |
| Replaced logical bytes | `[0x3820, 0x5820)` within IP.BIN, copied exactly from the historical bootstrap |
| Physical sectors changed | Data-session sectors 7–11, zero-based; logo bytes and their EDC/P/Q ECC only |
| Everything else | Byte-for-byte identical, including the session descriptor, filesystem, boot header and executable |
| Descrambled executable SHA-256 | `762fff4611658023479c3c539aa7e87367ca928b555bee4fa446136e5fed34b2`, identical to the source build |
| Integrity checks | EDC and P/Q ECC pass for all 195 bootstrap/metadata/executable sectors in both images |

The historical 8,082-byte MR has a single trailing palette-index byte after
all 28,800 pixels. KOS makeip's `gimp/file-mr.py` explicitly accommodates this
legacy convention. Validation of the pixel stream excludes that byte; the
diagnostic image preserves the original bytes exactly. The generated K-UI MR
passes the existing strict validator without this accommodation. The production
branding validator and tracked bootstrap are unchanged.

The stock-badge control was run in the same Flycast harness with fresh state and
the retail BIOS. It reached the KallistiOS banner, controller setup and video
initialization, then waited at the same unsupported SCI receive operation
(`PC 0x8c039896`). The run was stopped after 45 seconds. It does not reproduce
the console failure and does not establish a complete menu boot.

For the next physical comparison, use the diagnostic CDI with the entire SD
adapter disconnected, matching the previous check. A successful boot would make
the logo path worth investigating further, although a different CD-R/burn is
also a variable. The same freeze would show that our custom badge is not required
to trigger it; startup and CD-R reading would remain open possibilities.
