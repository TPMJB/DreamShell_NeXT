# K-UI 1.0: a new interface and tools for the Dreamcast

I've been working on K-UI — Katana User Interface — and 1.0 is ready.
It started with improving GD-ROM dumping and grew into an overhaul of the
launcher and everyday tools.

What's in it:

- A coordinated interface with controller navigation and stick-pointer support.
- GD-ROM dumping with Stop/Resume, CRC verification and targeted recovery.
- FAT32/exFAT storage support, Games and ISO Loader.
- Redesigned File Manager, VMU Manager, Settings and diagnostic apps.
- A fixed bootloader/recovery screen and five original synth tracks.

I've been testing on real hardware. The latest app retest is working, but
there's one intermittent disc-dumping crash I haven't been able to reproduce
reliably, reported with MDK2. It's documented in the release notes. Game and
hardware compatibility still varies.

**[Download K-UI 1.0](https://github.com/TPMJB/DreamShell_NeXT/releases/tag/k-ui-1.0)**

Use `K-UI-v1.0.zip`, back up your existing files, and update the complete `DS`
folder. You can reuse an already-working boot disc for the SD update. The fixed
3.3 bootloader CDI is included for new installations; no BIOS flashing is needed.

K-UI is built on SWAT's DreamShell and KallistiOS, with upstream credits and
licenses retained. The old Classic launcher is retired from this build.

If you try it, I'd like to hear what works on your setup. For a crash, please
include the build, hardware, disc revision, `rip.log`/`kui-memory.log` where
available, and a photo of the exception screen.

The cover is promotional artwork; host-rendered app previews use sample data.

— TPMJB

[Source and guides](https://github.com/TPMJB/DreamShell_NeXT) ·
[Optional Ko-fi support](https://ko-fi.com/tpmjb)
