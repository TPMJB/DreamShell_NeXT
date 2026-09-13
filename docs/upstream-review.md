# Upstream review for NeXT 1.0.0

Reviewed DC-SWAT/DreamShell through `05b9ccbd6db97c253231f9cdb99be749db31f3b9`
on 2026-09-13. The shared upstream base is
`a8e72b46b335e4a76d5b62bfd0d34f1a99b9d4d5`; three newer DreamShell commits were
reviewed. This release is selective, not a merge of all upstream changes.

| Upstream change | Decision | Reason |
| --- | --- | --- |
| [SPI interface queries in video and Speedtest](https://github.com/DC-SWAT/DreamShell/commit/f64058aad09d1eb95ede7e29ae5bf8c2064237bc) | Backported | Replaces an indirect SPI error-code check with explicit SD/W5500 queries and includes W5500 in the existing PVR-DMA exclusion. |
| [GUI_RTF font cache](https://github.com/DC-SWAT/DreamShell/commit/3c88eb47828897c295ad19d337a1b5ec22af451c) | Deferred | Potentially useful for RTF documents; changes font ownership/lifetime and does not affect the current launcher or keyboard. Evaluate with an RTF app. |
| [Memtest activity LED](https://github.com/DC-SWAT/DreamShell/commit/05b9ccbd6db97c253231f9cdb99be749db31f3b9) | Deferred | Optional visual feedback using SPI chip-select pins, unrelated to normal boot/ripping. Needs the appropriate hardware test. |

The first DreamShell commit also bumps KallistiOS. The full comparison is
diverged and includes 25 commits on the new side, covering timers/toolchain
configuration, NAOMI 2 graphics, networking and other changes. NeXT keeps the
already-used pin `a78fa2a2761360d96b66ba913e447812d5f2b889`.

Only these two upstream KallistiOS commits are backported, as source patches
applied by `.github/scripts/dev-build.sh prepare`:

- [sd_get_interface](https://github.com/DC-SWAT/KallistiOS/commit/830ce26c4fb854f22e9039aea16e38743f6971a0)
- [w5500_adapter_interface](https://github.com/DC-SWAT/KallistiOS/commit/2bda829e96bb9627eeccbd48868897f5fcfb6408)

The previous `sci_spi_rw_byte(0, NULL)` check returns `SCI_ERR_PARAM` before any
byte transfer on an initialized SCI interface; it was not transmitting to the
card. The new queries identify the initialized device/interface directly.

The patches add the headers, exports and getters required by video/Speedtest.
They apply cleanly to the pin alongside `cdrom-timeout-deadlock.patch` and are
compiled by the full SH-4 build. They do not change the SD transfer algorithm or
FatFs allocation code. This is not a claimed explanation or proven fix for the
intermittent CRC/read-back reports.
