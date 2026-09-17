# E.G.G. completion crash investigation

## Hardware retest and 1.0.1 decision

The maintainer supplied `rip-2.log` / `verify-2.log` and `rip-3.log` /
`verify-3.log` after testing the patch. Both were fresh 516,324-sector rips,
with retries 10, zero-fill off, sector EDC on and advanced ECC off.

| Measurement | Run #2: previous build | Run #3: GD Ripper 2.2.5 |
| --- | --- | --- |
| Music state reported | Turned on after opening the app | On when opening the app |
| Memory released at verification music suspension | 952,136 bytes | 1,069,752 bytes |
| Sector-validation failures / bulk fallbacks | 69 / 69 | 0 / 0 |
| Extraction completion | 1,747.459 s | 1,775.933 s |
| Stream CRC result | Full TOSEC track match | Full TOSEC track match |
| Finalization | Reached `rip-finish` | All new milestones and `rip-finish` reached |

Run #2's eight diagnostic samples still show an eight-byte overwrite, now at
`0x8c80e970`–`0x8c80e977`, again the same cache index. Run #3 contains no sampled
corruption because it has no validation failures. Both reports identify
Elemental Gimmick Gear v1.001 (1999)(Vatical)(US)[!] and the same track CRCs:
`85e929ab`, `087ff89a`, `8277c8be`. These are streamed CRC checks, not a full
storage read-back.

The tested patch source is `c7cacb4399e2fed00e75f64c8ffe5aba4546f7fe`, which also
passed the [complete package build](https://github.com/TPMJB/K-UI_DS/actions/runs/35240170225).
The logs identify GD Ripper 2.2.5; they do not independently fingerprint the
loaded core. The prescribed test installation updates the complete DS folder.

This evidence supports shipping the cache correction in K-UI 1.0.1. The
maintainer authorized merging PR #15 based on the results. It does not prove
that the intermittent reboot is permanently eliminated or establish the exact
original overwrite mechanism. No ripping speed improvement is claimed.

## Initial failure

The supplied `rip(4).log` contains a fresh dump followed by a completed-file
resume. The fresh run writes all 516,324 sectors, returns from automatic
verification and records `RAM verify-finish`, but never records `RAM rip-finish`.
The resumed run skips all three completed tracks and reaches both markers.
This narrows the reported reboot to the final result display/cleanup, or an
earlier memory fault that becomes visible there. A logging failure could also
hide later execution; there is no exception register dump in this file.

About 9.3 MB is available at verification completion. Ordinary main-heap
exhaustion is not supported by those snapshots. Music has already been stopped
and freed, and its restart occurs after the missing final marker.

All eight captured bad-sector/reread pairs affect exactly eight bytes at
offsets 1872–1879 in bulk slot 14, whose sector pointer is `0x8c82a220`.
The absolute addresses are `0x8c82a970`–`0x8c82a977`. Every pair starts with
`40` changing to `00` and has CRC XOR `e9afb037`. This strongly suggests a
repeatable memory/cache overwrite rather than unrelated physical disc errors.
The individual rereads pass validation. It does not identify the writer or
establish that this corruption caused the final reboot.

## Framebuffer cache lead

There is a stronger, but still inferred, identification of the eight bytes.
Assume that the original bytes were zero and that the second corrupted word was
the SH-4 cache tag `0x0c828003` (the physical 16 KiB region containing the affected
buffer address, with valid/dirty bits set). Solving the CRC difference for the
first word yields `0x8c23ff40`. The resulting bytes `40ff238c0380820c` exactly
reproduce the observed CRC XOR and the logged first-byte change. All eight
candidate bytes are nonzero. The released core's `GetScreenOpacity` machine code
identifies `0x8c23ff40` as the address of `screen_opacity`, initialized to `1.0f`.
The complete byte sequence was not recorded, so this is not a direct observation.

The rendering path naturally encounters both values: `VideoThread` writes back
its framebuffer before a PVR DMA upload, then reads `screen_opacity` to draw the
screen. The approximately 1 MiB writeback exceeds the 65,560-byte threshold in
[the pinned KOS cache implementation](https://github.com/DC-SWAT/KallistiOS/blob/a78fa2a2761360d96b66ba913e447812d5f2b889/kernel/arch/dreamcast/include/arch/cache.h),
which substitutes a read/modify/write of every operand-cache tag. The change
uses individual cache-line writebacks covering the framebuffer instead, retaining
DMA coherency while avoiding that global shortcut and unrelated ripper buffers.

This is a targeted mitigation, not proof of the original overwrite mechanism.
The cache-tag writes use associative mode; a simple tag-retargeting race has not
been demonstrated. Hardware behavior, including rendering performance, still
needs confirmation.

## Diagnostic changes in GD Ripper 2.2.5

- Record the verification result and whether its report was saved.
- Log before and after result presentation and final drive cleanup.
- Skip a redundant second drive-stop command after the pre-verification stop
  succeeds. Failed stops and early error/cancellation paths retain cleanup.
- Record up to 32 bytes before and after each of the existing eight diagnostic
  samples. This adds no disc reads and does not change retries or validation.

These changes were prepared as diagnostics, a cache mitigation and limited
cleanup. The hardware retest above supports the 1.0.1 release decision, while
the duplicate drive stop has not been established as the crash cause.

## Preserve the existing dump

Keep the dump folder, including `rip.log`, `verify.log`, `rip.state` and the CRC
sidecars. The automatic check uses saved streaming CRCs; it does not reread the
whole dump from storage. `verify.log` is needed to establish the actual catalog
result. The uploaded rip log alone cannot establish a matching dump.

After installing a diagnostic build, one **Start / Resume** with the same disc
and folder can exercise finalization without another full rip. The supplied log
already contains one successful resume, so another successful resume cannot
prove that the fresh-rip crash is fixed. A manual **Scan saved dump** is a
different path and does not exercise the new rip-finalization milestones.

If a reboot recurs, retain the resulting log and photograph any exception
screen. The current core waits only five seconds before rebooting, so that
screen can be missed. Do not discard or repeat a complete dump just to obtain
these files.
