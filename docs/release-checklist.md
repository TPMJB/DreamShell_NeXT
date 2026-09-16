# K-UI 1.0 release preparation

`codex/k-ui` is the candidate; merge to `master` after console approval. `VERSION` is **1.0**; the intended release
tag is **k-ui-1.0** and the complete archive is `K-UI-v1.0.zip`.
The older tag `v1.0.0` is a historical development milestone preceding 0.9 and
0.9.1. It is not the upcoming 1.0 release and must not be moved or reused.

## Source preparation

- [x] Include the complete 0.9.1 source, FatFs fixes and all integrated apps.
- [x] Include GD Ripper 2.2.4 recovery reporting while retaining Games destinations.
- [x] Include TPMJB build attribution and the current screenshots/Ko-fi links.
- [x] Replace the inherited CLA process with the NeXT contribution policy.
- [x] Retain upstream and third-party licenses and required notices.
- [x] Provide current install/build guides and an explicit compatibility record.

## Release gates

- [ ] Test K-UI 3.3 boot artwork/recovery, all app labels/focus states, and the
  five-track selection on real hardware. Older 3.2 feedback does not validate
  the new artwork or playlist.
- [ ] Rename the GitHub repository to `K-UI` and update canonical links after
  the rename. Existing repo links remain live while this branch is tested.


- [x] Run the full host suite on the K-UI candidate: all 168 tests passed
  on 2026-09-16, including Linux FAT32/exFAT, boot artwork, playlist lifecycle,
  font upload, geometry and recovery checks. CI uses the sanitizer defaults; local LeakSanitizer detection is
  disabled for the host environment.
- [ ] Run **Full K-UI release** from `codex/k-ui` with publication disabled; retain
  the successful run URL and exact source commit.
- [ ] Confirm complete SH-4 build, runtime imports, package contents, attribution
  records, and both CDI checks pass in that run.
- [x] Record maintainer feedback for bootloader 3.2: "New bootloader works great!"
  (2026-09-16). Detailed cases below still need individual results.
- [ ] Complete detailed bootloader checks with no SD: check readable K-UI recovery text, insert SD,
  press X, then A to boot; also check Start-held recovery and a failed core load.
- [ ] On the candidate, check Settings save/reboot, GD Play disc changes/exit,
  ISO Loader navigation and launching a known-working game from Games.
- [ ] Check rip destination discovery, recovery counts across reopen, a file copy
  and a VMU backup/restore with nonessential data. Reuse existing known-good dumps.
- [ ] Test launcher Off/On and 75%/100% controls; compare a known-good rip with
  music on/off for catalog CRC, throughput and sound. The 3.2 CD can be reused.
- [ ] Record hardware results or remaining limitations in `compatibility.md`.
- [ ] Review `RELEASE-NOTES.md`; its claims must match the completed checks.
- [ ] When ready to publish, run the same workflow from the reviewed `master`
  commit with publication enabled. It refuses an existing `k-ui-1.0` tag or a source
  branch that changes during the build.
- [ ] After publication, update the README's stable download/source links to `k-ui-1.0`.

The default workflow action produces an artifact without a GitHub release or tag.
Pushing a source or documentation change does not run the full Dreamcast build.
Firmware writes and every possible hardware combination are not requirements
for this candidate test; the documented limits must remain visible.
