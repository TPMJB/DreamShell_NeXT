# GD Ripper 2.1.1 saved-read diagnostic

This is a diagnostic build for a confirmed console/PC discrepancy, not a claim
that the underlying storage or memory fault has been fixed. It is based on the
working exFAT preview at `6958fdc`. The update contains only GD Ripper and this
guide. Keep the installed DreamShell core, boot disc and launcher for this test.

Two copies of Elemental Gimmick Gear Track 1 (`CC11` and `CC22`) both have PC
CRC32 `85e929ab` and size 26,721,072. On Dreamcast, CC11 returned that CRC with
zero suspect sectors; CC22 returned `7a27c049` with four suspect sectors. Both
console runs used storage read-back, not CRC journals. This rules out a stale
journal as the sole cause. It does not yet identify the faulty instruction,
prove the filesystem is responsible, or distinguish repeatable bad reads from
intermittent corruption. The new launcher was not involved in those tests.

## Install and run the short test

1. Power down. Extract `DreamShell-crc-diagnostic-update.zip` and merge its `DS`
   folder onto the card, overwriting the GD Ripper files. Confirm **2.1.1 diagnostic**.
2. Select destination `/sd`, folder `CC22`, then **Advanced features > Scan saved
   dump**. No disc is required. Do not use Start / Resume on these single-track
   diagnostic folders.
3. Save `verify.log`, `readback.log`, `readback.bin` and `track01.bin.suspect`
   from CC22 before scanning that folder again. Then scan CC11 and save the same
   files from its folder. Preserve the folder names when zipping the reports.
4. Both original track files should still hash to `85e929ab` on the PC.

The scan opens tracks read-only. It never changes their bytes, CRC journals,
`rip.state` or `rip.complete`. The normal scan's suspect map and verification
report are still rewritten. `readback.log` and `readback.bin` are new diagnostic
outputs and are replaced by the next manual scan in that folder.

## What the diagnostic measures

- Records the first scan's CRC even when later reads differ; rereads cannot
  silently turn the reported first pass into a catalog match.
- For up to 32 suspect sectors per scan, captures the first observed 2352 bytes
  and reopens the track twice to reread that exact file offset into a second
  aligned buffer. Each observation is saved separately. Reopening discards the
  file handle's sector buffer; it does not bypass every hardware cache.
- Logs each reread's CRC, validation flags, differing-byte count and first/last
  difference offsets. Up to 32 individual byte differences are shown in text;
  the complete sampled sectors are in the binary file.
- Checks the scan buffer's CRC around sector validation, diagnostic/map writes,
  hashing, and the UI update/thread yield. A difference across these checks is
  reported separately from disagreeing file reads.
- Reports **Storage reads disagree** if either check detects inconsistency.
  This prevents the UI from recommending disc repair for a known inconsistent
  storage observation. `readback.unstable` also blocks Start / Resume and disc
  repair in that folder until a full, consistent manual scan clears the marker.
  Cancelled/failed scans and fast stream-CRC checks do not clear it.

An unchanged invalid sector on all three reads remains a suspect sector. That
is evidence of repeatability, not proof the bytes are stored incorrectly: compare
the captured samples with the PC file. A clean diagnostic pass also cannot prove
an intermittent problem is gone; the extra checking changes execution timing.
Audio and unsupported sector formats do not get targeted EDC/ECC rereads.

## Reading the captures

`readback.bin` is a concatenation of 2352-byte samples, limited to 225,792 bytes
(32 sectors × three observations). `readback.log` identifies each track, sector,
FAD, source-buffer address and sample offset. Pass 0 is the original observation;
passes 1 and 2 are reopen/rereads. Flags are sync=1, address=2, EDC=4, ECC=8 and
unsupported=16. Sample CRCs allow their bytes to be independently checked on PC.

The console report adds `readback_disagreements`, `buffer_changes`,
`diagnostic_sectors`, `diagnostic_skipped` and `diagnostic_written`. Once the
sample limit is reached, normal CRC/sector scanning continues and the skipped
count makes the diagnostic limit explicit. Observations before the first CRC
sample, between checks, or reproduced identically on rereads may escape these
checks. The instrumentation does not change the SD transport, filesystem,
cache configuration or CRC algorithm.
