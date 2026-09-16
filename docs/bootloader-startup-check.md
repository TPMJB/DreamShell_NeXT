# Bootloader disc checks — 2026-09-16

The reported console freeze on the SEGA license screen remains unresolved.
The maintainer identified the original K-UI bootloader CDI as the burned image,
then reported that the old bootloader disc also failed and that startup still
hung with the SD card removed. Which disc was used for the no-SD comparison, and
whether the adapter itself was removed, have not yet been established.

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

No additional firmware change or new disc burn is justified by these checks
alone. The remaining useful physical comparisons are the previously working 3.2
disc with the entire SD adapter disconnected, and a known-working pressed game
disc. A pressed-game success alone would not verify CD-R readability.
