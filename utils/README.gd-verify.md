# DreamShell GD Dump Verifier

## Verify directly on the Dreamcast

GD Ripper 1.9 adds two on-console paths:

- Leave **Verify against known dump after rip** checked to read the files back
  and verify them immediately after a successful rip.
- Enter the existing dump's folder name and select **Verify** to check it later;
  the game disc is not needed for this read-back pass.

The console checks `rip.state`, `rip.complete`, track sizes, and `.bad` maps,
then calculates CRC32 for every track. Results appear on screen and in
`verify.log` inside the dump folder. The database is read as a stream, so it
does not need to fit in the Dreamcast's memory.

The bundled `DS/apps/gd_ripper/redump.db` is generated from Libretro's reduced
Dreamcast catalog. It normally contains one identifying data track per game,
so a successful result is **IDENTIFIED BY DATA TRACK**. This is a known-dump
content match, but not a full physical-disc Redump verification.

To replace it with a current or full Redump-compatible DAT, run this on a PC:

```sh
python3 make_gd_redump_db.py "/path/to/Sega - Dreamcast.dat" \
  --output redump.db
```

Copy the resulting file over `DS/apps/gd_ripper/redump.db` on the DreamShell
card. ZIP- and gzip-compressed DAT files are accepted directly.

For direct comparison, GD Ripper data tracks must use 2352-byte BIN format.
With 2048-byte ISO data tracks, the console reports
**INCOMPATIBLE DATA-TRACK FORMAT** and retains the computed hashes in the log.

## Verify on a desktop computer

`verify_gd_dump.py` checks a GD Ripper folder on a desktop computer. It never
uploads disc data: all parsing and hashing happen locally with Python's standard
library.

The verifier deliberately keeps these results separate:

- **FULL TRACK MATCH** — every dumped track matches one Redump DAT entry.
- **DATA TRACKS MATCH** — all data tracks match, but one or more audio tracks do
  not. This is common when a console dump and a Redump-grade PC drive use
  different audio offsets or padding.
- **IDENTIFIED BY DATA TRACK** — a reduced DAT (including common Libretro DATs)
  lists only an identifying data track. This identifies the game/revision but
  does not verify every track.
- **NO DATA-TRACK HASH MATCH** — no catalog entry has an identical data track.
- **INCOMPATIBLE DATA-TRACK FORMAT** — DreamShell produced 2048-byte ISO data
  tracks. Enable **Use bin tracks** before ripping if you want direct comparison
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

## Compare two DreamShell rips

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

For the most complete answer, use Redump's full Dreamcast DAT containing every
track. A reduced DAT such as Libretro's `Sega - Dreamcast.dat` is still useful,
but generally contains only one identifying data track per game; the verifier
therefore reports **IDENTIFIED BY DATA TRACK**, never a full-disc match, when
that is all the catalog provides. The reduced DAT can be downloaded directly
from
<https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/redump/Sega%20-%20Dreamcast.dat>.

## Privacy and performance

Nothing is sent over the network. Hashing reads each track once and calculates
all three digests together. A typical GD-ROM is limited mainly by the speed of
the SD card or storage device connected to the computer.
