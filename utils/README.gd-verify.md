# DreamShell GD Ripper 2.2.2

## Recovery reporting in 2.2.2

Recovery, the Start / Resume prompt and on-console verification now distinguish
**originally flagged**, **recovered** and **unresolved** sectors. Counts include
repairs from earlier sessions, including interrupted recovery. `verify.log`'s
`bad_sectors` field is the current unresolved count; `recovery_flagged`,
`recovery_recovered`, `recovery_remaining` and `recovery_pending` record the details.
Unreadable or invalid recovery metadata reports counts as unavailable, never zero.

Existing dumps need no conversion. Reporting reads the original target list and
validates saved data sectors or audio confirmation records. It does not rewrite
tracks, delete recovery records or read the entire track. Stream CRC verification
still does not reread untouched sectors. Keep `.bad`, `.recovery-base`,
`.recovery-audio`, `.repair-backup` and CRC files with your dump.

If the last sector was repaired before an interruption prevented the final
checkpoint, the prompt shows **0 unresolved** and **Finish recovery**. The dump
stays incomplete until finalization succeeds. Repeated recovery does not treat
the original error total as the remaining workload.

GD Ripper 2.2.2 is included in the complete NeXT 1.0 installation. See the
[installation guide](../docs/installation.md). A working NeXT 3.0/3.1 boot disc
can be reused. Existing partial dumps retain their saved recovery progress.

## First pass, then optional recovery

Enable **Advanced features > Recover damaged disc** before Start / Resume.
This optional mode uses raw BIN and full Mode 1 address/EDC/ECC checks.
Readable sectors are saved normally. After a failed bulk read, individual
sectors get two attempts; those still unreadable are recorded in `.bad` and
temporarily represented by zeros so the rest of the disc can be collected.
These placeholders are unresolved holes, never recovered data. Normal ripping
with this mode OFF retains its existing retry/stop behavior.

After the first pass, a damaged dump displays **THIS DUMP'S A MESS.** with current
unresolved and original/recovered counts, plus **Try recovery / Later** buttons. The app does not
create `rip.complete` while recovery holes remain. A clean first pass goes
straight to the normal catalog CRC check without a prompt.

Choosing **Try recovery** runs the selected pass limit (default 10, maximum 50)
against unresolved sectors only. Each sweep tries each remaining location,
alternates forward/backward order and reinitializes the drive between passes.
Successful data reads must pass the existing sector address, EDC and P/Q ECC;
the ripper does not manufacture parity to make damaged data look valid. Audio
requires two matching reads. The drive may cache these reads, so agreement is
not proof of independence; the final catalog CRC remains the reference.
Each patched sector is backed up, flushed and read back from storage.

**Stop** preserves progress. **Later** returns to the main screen. Keep the
same disc, destination and folder; **Start / Resume** offers the saved recovery
again, even after restarting the console. The app checks the disc identity
before modifying tracks. If the pass budget runs out, it shows the number still
unresolved and offers another recovery session. Drive/media changes and
storage/memory faults stop the operation with an error instead of being hidden
as ordinary bad-sector retries.

`rip.recovery` saves the mode, and `rip.first-pass` records completed acquisition.
Per-track `.recovery-base` files preserve a checksummed original CRC and target
list; `.recovery-audio` records confirmed audio reads. `.repair-backup` keeps
replaced bytes, and `.bad` continues to block clean verification until all its
targets are resolved. Preserve these files with the dump. A damaged published
baseline stops recovery rather than guessing at the previous contents.

After a repair, CRC replacement math updates the whole-track CRC without
reading unchanged portions of the track from SD. On resume it rereads only the
original target sectors to reconcile even an interrupted sector write. The
normal stream-CRC limitation still applies to the untouched data: this is not
a full storage read-back. Once all holes are resolved, the app creates the
completion marker and checks the resulting track CRCs against the catalogs.
A remaining catalog mismatch stays a mismatch. A whole-track hash cannot tell
us where to retry when every sector passes its internal checks.

## Sector validation

Normal raw Mode 1 ripping now always checks sync, address and EDC and retries
invalid reads. Advanced CRC adds ECC parity validation and repair of existing
suspects. The log records the selected checks; `verify.log` explicitly labels
an unperformed storage scan `NOT_RUN`. Stream CRC is sampled before writing
and checked again against the RAM buffer after the write. A detected mutation
stops with **Memory changed during write** and records the entire uncertain
write span in `.bad`. The previously trusted CRC prefix is retained; resume
cannot approve that span without a scan/repair. In this case `.bad` marks
untrusted output, not deliberate zero-fill. Both CRC samples still come from
RAM: they do not replace storage read-back or detect every possible transient
mutation, and old completed tracks are not revalidated on normal resume.

The narrowly targeted `repair_timestalkers.py` desktop tool remains included.
Its exact-input requirements and earlier diagnosis are documented in
[the historical recovery notes](../docs/timestalkers-recovery.md).

## Normal ripping

Insert a disc and wait for **Ready to rip**. GD Ripper detects the lid/disc
state, lets the drive settle, and reads its title automatically. There is no
manual Read Name button. Detection is suspended while ripping, repairing or
verifying; one worker owns all of the app's drive commands.

The default destination is `/ide/Games`, then `/sd/Games`, then `/pc/Games`,
depending on available devices. Device buttons create/open their Games folder,
and each disc gets its own folder. These locations are also scanned by Games.
You can browse to another destination or select an existing dump to resume.

Choose **Start / Resume**. CRC32 is calculated from the bytes successfully
written while ripping. On completion, the saved CRCs and track sizes are
compared with both the bundled TOSEC retail GDI and reduced Redump catalogs.
This fast path does **not** reread the track data from SD or prove that storage
returned the same bytes. `verify.log` explicitly identifies the hash origin.

Use the D-pad or stick to select a control and A to activate it. B requests
Stop or returns from a settings page. The controller does not emulate a mouse;
real mouse clicks and keyboard input remain available. Destination has SD,
IDE and PC buttons plus an editable path. Settings show ON/OFF in words.

## Advanced features

- **Recover damaged disc**: collect readable data first and ask before targeted
  recovery passes; see the workflow above. This mode forces BIN and ECC checks.
- **Advanced CRC (ECC + repair)**: adds ECC parity checks to the mandatory raw
  Mode 1 sync/address/EDC checks and enables repair of saved suspects. Invalid
  reads fall back to individual-sector retries before writing. The console
  accepts validated rereads and does not synthesize ECC. Mode 2 and audio are
  not covered by these checks.
- **Scan saved dump**: rereads storage, calculates whole-track CRC32, and scans
  raw Mode 1 data sectors. It writes `trackNN.bin.suspect` containing zero-based
  track-sector, FAD, and error flags (sync=1, address=2, EDC=4, ECC=8).
  No disc is required for a scan. Serial SD read-back can take about 30 minutes.
- **Zero-fill unreadable sectors** remains OFF by default. Turning it on writes
  zeros only after retries are exhausted and records them in `.bad`. It loses
  data and cannot count as a clean verification.
- Track format defaults to 2352-byte BIN. 2048-byte ISO cannot match these raw
  track catalogs. Read-attempt choices are 1, 5, 10, 20 and 50.

To use the older saved-dump repair path, leave Recover damaged disc OFF and
scan the dump first. If suspects were found,
insert the same disc, enable **Advanced CRC during rip**, keep the same
folder/destination, then choose **Start / Resume**. The app checks the TOC and
boot-sector identity, rereads flagged sectors, and accepts only reads that pass
address/EDC/ECC validation. Before each replacement it durably saves the old
2352 bytes in `.repair-backup` (ASCII `FAD <number>\n`, then 2352 raw bytes,
repeated). A failed replacement leaves the original or its backup available.
CRC checkpoints are invalidated before edits; the repaired track is hashed
from storage once to rebuild its whole-track CRC. This is intentionally a slow
advanced operation. A fully repaired zero-fill map is saved as `.bad.history`
before its active `.bad` map is removed.

No catalog match is **inconclusive**: different revisions, missing catalog
coverage, different track boundaries and read/storage errors can all cause it.
A whole-track CRC cannot reveal a bad-sector address. If the sector scan finds
no suspects but CRC still differs, compare two independent rips on a PC (below).
The checker never changes sectors just to force an expected whole-track CRC.

## Resume, Stop and storage

Stop returns to the interface immediately and requests cancellation. Keep the
console powered until it shows a stopped/result message and Exit is enabled.
The worker must still finish or time out its current I/O and save a checkpoint.
A waiting operation displays its elapsed time and FAD; a long drive read is
explicitly labeled stalled. Timed KOS commands now also bound semaphore waits.
A BIOS call that itself never returns cannot safely be interrupted by issuing
competing drive commands from another thread; hardware testing is still needed
for such firmware-level hangs.

`rip.state` retains the original track layout. `rip.disc` binds new GD-ROM
resumes to the 2048-byte boot sector; older dumps are checked against their
saved track 3 (or low data track if track 3 is empty). This is not a disc-wide
cryptographic identity and CD/CD-R resumes still use the existing TOC checks.

Each `.crc` journal records a metadata tag, committed byte count, rolling CRC
and record checksum, **after** the track is flushed. Torn or out-of-range
records are ignored. Resume restores the last valid checkpoint and hashes any
uncheckpointed tail. An older dump without a journal needs a one-time hash of
its saved portion. Keep the `.crc`, `.bad`, `.suspect` and `rip.*` files with
their tracks. A matching stream CRC does not test for subsequent SD corruption;
use the optional storage scan for that.

## Catalogs on the Dreamcast

The release includes `redump.db` and `tosec.db`; keep both with `app.xml` and
the module. TOSEC is a natural reference for traditional Dreamcast GDI dumps.
Redump data tracks are also useful when their boundaries and bytes agree.
For Sonic Adventure's common US revisions both catalogs have the same track 3:

| Revision | Bytes | CRC32 |
| --- | ---: | --- |
| v1.004 / original US | 1,185,760,800 | `21483b0c` |
| v1.005 / US Rev A | 1,185,760,800 | `00c55860` |

Thus changing catalog alone does not explain a mismatch in those tracks.
Sources: bundled database provenance in `redump-db.README`; [TOSEC official
DAT pack](https://www.tosecdev.org/downloads/category/59-2025-03-13) and
[Libretro Redump DAT](https://github.com/libretro/libretro-database/blob/master/metadat/redump/Sega%20-%20Dreamcast.dat).

To update either database on your PC:

```sh
python3 make_gd_redump_db.py "Sega - Dreamcast.dat" -o redump.db
python3 make_gd_redump_db.py "TOSEC US.dat" "TOSEC JP.dat" "TOSEC PAL.dat" -o tosec.db
```

Copy the result into `DS/apps/gd_ripper/`. XML, ClrMamePro, gzip, and ZIPs
containing a single DAT are supported. Extract regional DATs from a full TOSEC
pack first. The compact format remains backward compatible with v1.9.

## Verify on a desktop computer

`verify_gd_dump.py` checks a GD Ripper folder on a desktop computer. It never
uploads disc data: all parsing and hashing happen locally with Python's standard
library.

The verifier deliberately keeps these results separate:

- **FULL TRACK MATCH** — every dumped track matches one selected DAT entry.
- **DATA TRACKS MATCH** — all data tracks match, but one or more audio tracks do
  not. This is common when a console dump and a Redump-grade PC drive use
  different audio offsets or padding.
- **IDENTIFIED BY DATA TRACK** — a reduced DAT (including common Libretro DATs)
  lists only an identifying data track. This identifies the game/revision but
  does not verify every track.
- **NO DATA-TRACK HASH MATCH** — no catalog entry has an identical data track.
- **INCOMPATIBLE DATA-TRACK FORMAT** — DreamShell produced 2048-byte ISO data
  tracks. Select **Track format: BIN** before ripping if you want direct comparison
  with Redump's 2352-byte BIN hashes.

Even a full track-file match does not mean DreamShell captured lead-in,
lead-out, raw subchannel data, or independently measured drive/disc offsets.
Those require Redump's supported PC-drive workflow. The result means the files
listed in the DAT are byte-identical.

## Quick start

Python 3.8 or newer is sufficient; no packages need to be installed.

Linux or macOS:

```sh
python3 verify_gd_dump.py "/path/to/EVOLUTION_-_THE_WORLD_OF_SACR" \
  --dat "/path/to/Sega - Dreamcast.dat"
```

Windows PowerShell:

```powershell
py .\verify_gd_dump.py "E:\EVOLUTION_-_THE_WORLD_OF_SACR" --dat "$HOME\Downloads\Sega - Dreamcast.dat"
```

The first argument may be either the dump folder or its `.gdi` file. DATs may
be Logiqx XML or ClrMamePro text and may be uncompressed, `.zip`, or `.gz`.
The current Redump Dreamcast DAT can be obtained from
<https://redump.org/downloads/>.

The verifier prints CRC32, MD5, and SHA-1 for every track, then exits with:

- `0`: clean DreamShell metadata and the requested comparison passed;
- `1`: incomplete/untrusted dump, hash mismatch, or partial-only result;
- `2`: invalid input or an unreadable file.

## Find mismatched sectors on a PC

### Locate suspect data sectors on a PC

`scan_gd_sectors.c` scans a saved 2352-byte Mode 1 data track for sync,
address, EDC and ECC errors and calculates its CRC32 from storage. It opens
the track read-only, requires no Dreamcast or third-party libraries, and can
be compiled with the C compiler on Linux or macOS:

```sh
cc -O3 -std=c99 scan_gd_sectors.c -o scan_gd_sectors
./scan_gd_sectors /path/to/TIME_STALKERS/track03.bin 45150 f92c1222 \
  > timestalkers-sector-scan.txt
```

The second argument is the track's starting **FAD**, taken from `rip.state`.
If using a GDI's starting LBA instead, add 150. The optional third argument is
the expected eight-digit track CRC32. Time Stalkers US v1.002 has an expected
Track 3 CRC of `f92c1222` in both bundled catalogs. Use the correct catalog
entry and FAD for other tracks and revisions; audio and Mode 2 are not covered.

Progress appears in the terminal; the redirected report lists every suspect
range with inclusive, zero-based track-sector indices and absolute FADs.
Flags match the console scan: sync=1, address=2, EDC=4, ECC=8, unsupported=16.
Exit codes are 0 for successful requested checks, 1 for sector errors,
unsupported sectors or a catalog CRC mismatch, and 2 for an incomplete scan
or invalid input. A report explicitly says when no catalog CRC was supplied.

Passing sector checks with a different catalog CRC remains a mismatch:
internally consistent bytes can still differ from a known dump. The report
does not modify the dump, create a console repair map or identify a hardware
cause. Review it before deciding what to reread.

### Compare independent reads

Two independent rips that match each other are useful evidence even when an
audio track cannot directly match Redump because of offset handling:

```sh
python3 verify_gd_dump.py "/path/to/first/rip" --compare "/path/to/second/rip"
```

If files differ, the tool reports the first different payload byte and sector.
Use different source media or clean/reseat the disc between attempts if the
goal is an independent read.

## DreamShell integrity checks

Before consulting a DAT, the tool checks:

- all files named by the GDI exist and have sector-aligned sizes;
- each file has exactly the size recorded in `rip.state`;
- `rip.complete` exists;
- no `trackXX.bin.bad` map records zero-filled or untrusted sectors.

You can run those checks without a DAT:

```sh
python3 verify_gd_dump.py "/path/to/dump"
```

`rip.complete` says GD Ripper reached the end of every track. It is not a hash
verification marker. A `.bad` file is intentionally a hard failure even if the
rest of the data tracks match.

## Which DAT should I use?

Use a TOSEC Dreamcast GDI DAT for traditional console dumps, and Redump when
track representation agrees. Both are supported. A full DAT can compare every
listed track; a reduced Libretro DAT normally covers one identifying data
track, so it cannot verify the entire disc. Catalog hashes do not identify
sector addresses or repair missing bytes. Neither format guarantees that an
unmatched dump is corrupt.

## Privacy and performance

Nothing is sent over the network. Hashing reads each track once and calculates
all three digests together. A typical GD-ROM is limited mainly by the speed of
the SD card or storage device connected to the computer.
