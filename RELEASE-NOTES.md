# K-UI 1.0.1 — GD Ripper stability update

![K-UI — Katana User Interface](docs/launch/1.0/k-ui-release-banner.jpg)

K-UI 1.0.1 addresses the repeated buffer corruption observed during GD-ROM
ripping and improves GD Ripper's completion handling. It includes **GD Ripper
2.2.5** and the existing **Bootloader 3.3**.

## What changed

- **Display cache handling:** screen uploads now write back only the framebuffer's
  cache lines. This avoids the previous whole-cache shortcut that also touched
  unrelated memory while ripping.
- **Cleaner completion:** GD Ripper avoids issuing a second drive-stop command
  after the pre-verification stop has succeeded. Cleanup still retries a failed
  stop and runs on cancellation or earlier errors.
- **Useful failure logs:** `rip.log` records the verification result, result-display
  and final cleanup stages, and bounded before/after bytes for sampled buffer
  corruption. Existing retries, checkpoints and dump formats are retained.

## Console results

Two fresh E.G.G. rips used the same settings with music loaded:

| Result | Previous build | Updated build |
| --- | --- | --- |
| Recovered sector-validation failures | 69 | **0** |
| TOSEC track CRC matches | All 3 | **All 3** |
| Rip and final cleanup | Completed | **Completed** |

The updated run completed all 516,324 sectors without a bulk-read error. Its
full TOSEC match covers the streamed track CRCs; it was not a full SD read-back.
The same source also passed the [complete Dreamcast build](https://github.com/TPMJB/K-UI_DS/actions/runs/35240170225).

This is encouraging evidence for the cache correction. **One successful fresh
rip cannot prove the previously intermittent reboot is permanently eliminated.**
If it recurs, keep `rip.log`, `verify.log`, the build identity and any exception
photo. See the [investigation and hardware results](docs/gd-ripper-egg-crash.md).

## Install or update

1. Power down and back up your current `DS` folder and personal settings.
2. Extract **K-UI-v1.0.1.zip** and copy its entire **DS** folder to the root of
   your SD or IDE/CF device, replacing matching program files.
3. **Update `DS/DS_CORE.BIN` as well as the apps.** The display-cache correction
   is in the core; an app-only update does not install it.
4. Safely eject and boot the updated core from storage. GD Ripper shows **2.2.5**.
   Keep existing dumps and their CRC/recovery sidecars together.

Your already-working bootloader disc can load this update. **No new bootloader
CD is required** for an existing working SD/IDE installation. For a full CD
installation, use **K-UI-v1.0.1.cdi**. No BIOS flashing or dump conversion is needed.

All K-UI 1.0 apps, artwork, music and existing game/device support remain in the
complete package. Existing compatibility limits, including Bust-A-Move 4 on
serial SD, are documented in the [compatibility record](docs/compatibility.md).
See the [installation guide](docs/installation.md) and [build guide](docs/building.md).

K-UI by TPMJB. The inherited core/API remains DreamShell 4.0.5 Beta 3.
Original attribution and third-party terms remain in [NOTICE](NOTICE) and
[licensing](docs/licensing.md).
