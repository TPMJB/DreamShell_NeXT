# Historical Time Stalkers diagnosis and recovery

These notes concern a specific earlier dump and older app/core defects. They
are retained for that diagnosis, not as installation instructions for NeXT 1.0.
Use the [current installation guide](installation.md) for an upgrade.

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

For current installations, follow the upgrade above. Keep the
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
