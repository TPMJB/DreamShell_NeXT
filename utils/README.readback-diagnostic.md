# GD Ripper saved-read diagnostics

These diagnostics remain in GD Ripper 2.2.2 and NeXT 1.0. They detect disagreements
between saved-file reads; the recovery-count reporting fix does not establish
a cause for earlier intermittent console/PC discrepancies.

## Collect a report

1. Use the complete current NeXT installation. Select the existing dump through
   **Destination**, then **Advanced features > Scan saved dump**. No disc is needed.
2. Preserve `verify.log`, `readback.log`, `readback.bin` and suspect maps before
   scanning the same folder again. Compare track hashes independently on a PC.
3. If reads disagree, retain the dump and diagnostics. A storage-inconsistency
   guard blocks disc repair until a consistent manual scan clears it.

## Earlier EGG observation

Two copies of Elemental Gimmick Gear Track 1 (`CC11` and `CC22`) both have PC
CRC32 `85e929ab` and size 26,721,072. On Dreamcast, CC11 returned that CRC with
zero suspect sectors; CC22 returned `7a27c049` with four suspect sectors. Both
console runs used storage read-back, not CRC journals. This rules out a stale
journal as the sole cause. It does not yet identify the faulty instruction,
prove the filesystem is responsible, or distinguish repeatable bad reads from
intermittent corruption. The new launcher was not involved in those tests.

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
