# E.G.G. completion crash investigation

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

## Diagnostic changes in GD Ripper 2.2.5

- Record the verification result and whether its report was saved.
- Log before and after result presentation and final drive cleanup.
- Skip a redundant second drive-stop command after the pre-verification stop
  succeeds. Failed stops and early error/cancellation paths retain cleanup.
- Record up to 32 bytes before and after each of the existing eight diagnostic
  samples. This adds no disc reads and does not change retries or validation.

These changes are diagnostics and limited cleanup, not a confirmed crash fix.
The duplicate drive stop has not been established as the cause. Do not describe
this build as hardware verified or merge it into a frozen release on that basis.

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
