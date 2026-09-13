# DreamShell GD Ripper 2.0.3

## Upgrade for mandatory sector checks

Merge the release's `DS/apps/gd_ripper` folder onto the card and confirm GD
Ripper **2.0.3**. A working 2.0.2 installation already has the required core;
no bootloader update is required. For older installations, merge the entire
release `DS` folder so the core and app are updated together.

The supplied Time Stalkers dump had 623 invalid data sectors, all at index 12
within 16-sector read blocks. Each had bytes 368..375 overwritten with one of
two repeating values. Recovering only those bytes from the unchanged P parity
made every affected sector pass its original EDC, P and Q checks. Combining
those corrections with the measured input CRC predicts `f92c1222`, the known
TOSEC/Redump Track 3 CRC. This identifies recoverable corruption within the
track files, not omitted lead-in or subchannel data.

The two repeating values resemble a DreamShell video-global pointer followed
by an SH-4 cache tag. That points toward corruption in the console's memory
path, but does not establish the exact offending instruction or its timing.
An initially suspected cache-tag race was not established: associative cache
writes compare tags instead of blindly replacing them. No speculative kernel
patch is included. Mandatory EDC checks close the confirmed unchecked-read
gap; a fresh hardware rip and an independent PC CRC are still required to
assess whether further corruption occurs, including after validation.

Normal raw Mode 1 ripping now always checks sync, address and EDC and retries
invalid reads. Advanced CRC adds ECC parity validation and repair of existing
suspects. The log records the selected checks; `verify.log` explicitly labels
an unperformed storage scan `NOT_RUN`. Stream CRC is sampled before writing,
then committed after a successful write. It still does not replace storage
read-back or revalidate an old completed track on resume.

### Recover the diagnosed Time Stalkers dump on a PC

```sh
python3 repair_timestalkers.py /path/to/TIME_STALKERS/track03.bin \
  "$HOME/track03.repaired.bin"
```

This restricted tool requires the original 1,185,760,800-byte file with CRC
`3303fcd5` and the diagnosed 623 overwrites. It opens the source read-only,
recovers the eight missing payload bytes using existing ECC, and writes a new
copy. The original EDC and ECC bytes are retained and must validate. Both the
computed output CRC and a read-back of the saved output must equal `f92c1222`.
An existing output is never overwritten; an unsuccessful partial copy is
removed. Allow about 1.2 GB of free space for the new file. The program refuses
other input CRCs, additional damage or a different number of repairs.

After it prints `VERIFIED`, use the repaired file as `track03.bin` in a **PC
copy** of the dump folder, alongside the matching Tracks 1 and 2 and its GDI.
Keep the original folder intact. Do not copy the old `track03.bin.crc` into
the repaired folder: that journal describes the original corrupted bytes.
If putting a repaired copy back on the Dreamcast, its missing CRC journal will
require one storage hash before fast resumed verification can be used.

## Install and diagnose an early stop

Version 2.0.2 fixes **Rip log creation failed** and the earlier stop at the end
of Track 1. The pinned FAT implementation maps `O_CREAT` to `FA_OPEN_ALWAYS`,
but omits that flag from the branch that creates a missing file. It can reopen
an existing file, so 2.0.1's storage probe passed while the first log or CRC
journal could not be created. The app now opens existing metadata for writing,
or explicitly creates a missing file with `O_CREAT | O_EXCL`, then seeks to
its end. Existing records are preserved. The fix is tested using the production
logger/checkpoint code and the pinned FatFs on in-memory FAT16/FAT32 volumes.

For current installations, follow the 2.0.3 upgrade above. Keep the
existing dump files; **Start / Resume** can continue a partial rip in the same
folder. Previously written invalid sectors require a scan/repair or the
restricted Time Stalkers recovery described above.

For a first installation or an upgrade from an older recovery build:
Merge the release's entire `DS` folder onto the SD card, including `DS_CORE.BIN`.
The bootloader and the loaded DreamShell core are separate binaries. During
boot, press/hold Start to enter the boot menu, select **Boot from SD**, and use
left/right to show the full path. Confirm `/sd/DS/DS_CORE.BIN` before pressing A.
Updating only the ripper or the bootloader can leave an older core running.

The ripper checks create/reopen/seek/sync/read-back on a small temporary file
before changing a dump. Old FAT handlers may reject append opens or refuse to
reopen an existing file for writing. Metadata now uses explicit seek-to-end;
cores that still cannot reopen files stop before track extraction with a core
update hint. Successful probe files are removed. This is a capability check,
not a test of the whole SD card.

The rip log must be created and reopened successfully before tracks are read.
Errors now distinguish log creation, CRC checkpoint, track sync/open/write,
sector-mode selection and exhausted disc reads, with filesystem or drive error
codes and the current track/FAD. Failed sector-mode selection gets bounded
reinitialization/retry attempts. Error messages stay visible during cleanup.

For Time Stalkers, the reported 300-sector Track 1 is exactly 705,600 bytes:
that size means the track reached its expected length. An error there can be
the final CRC checkpoint or the transition to Track 2, not necessarily a bad
Track 1 read. Keep that file; resume can hash it once, save its missing CRC,
skip Track 1, and proceed to Track 2. If the new build stops, preserve `rip.log`
and photograph its specific error message rather than starting over.

## Normal ripping

Insert a disc and wait for **Ready to rip**. GD Ripper detects the lid/disc
state, lets the drive settle, and reads its title automatically. There is no
manual Read Name button. Detection is suspended while ripping, repairing or
verifying; one worker owns all of the app's drive commands.

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

To repair a previously completed dump, scan it first. If suspects were found,
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
- `1`: incomplete/zero-filled dump, hash mismatch, or partial-only result;
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
- no `trackXX.bin.bad` map records zero-filled unreadable sectors.

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
