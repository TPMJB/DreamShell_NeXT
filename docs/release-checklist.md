# NeXT 1.0 release preparation

`master` is the integration branch. `VERSION` is **1.0**; the intended release
tag is **1.0** and the complete archive is `DreamShell-NeXT-v1.0.zip`.
The older tag `v1.0.0` is a historical development milestone preceding 0.9 and
0.9.1. It is not the upcoming 1.0 release and must not be moved or reused.

## Source preparation

- [x] Include the complete 0.9.1 source, FatFs fixes and all integrated apps.
- [x] Include GD Ripper 2.2.2 recovery reporting while retaining Games destinations.
- [x] Include TPMJB build attribution and the current screenshots/Ko-fi links.
- [x] Replace the inherited CLA process with the NeXT contribution policy.
- [x] Retain upstream and third-party licenses and required notices.
- [x] Provide current install/build guides and an explicit compatibility record.

## Release gates

- [x] Run the full host suite on the integrated source, including Linux exFAT
  checks: 167 tests passed on 2026-09-15. Local LeakSanitizer detection was
  disabled for the host environment; CI uses the test defaults.
- [ ] Run **Full NeXT release** from `master` with publication disabled; retain
  the successful run URL and exact source commit.
- [ ] Confirm complete SH-4 build, runtime imports, package contents, attribution
  records, and both CDI checks pass in that run.
- [ ] On the candidate, check Settings save/reboot, GD Play disc changes/exit,
  ISO Loader navigation and launching a known-working game from Games.
- [ ] Check rip destination discovery, recovery counts across reopen, a file copy
  and a VMU backup/restore with nonessential data. Reuse existing known-good dumps.
- [ ] Record hardware results or remaining limitations in `compatibility.md`.
- [ ] Review `RELEASE-NOTES.md`; its claims must match the completed checks.
- [ ] When ready to publish, run the same workflow from the reviewed `master`
  commit with publication enabled. It refuses an existing `1.0` tag or a source
  branch that changes during the build.
- [ ] After publication, update the README's stable download/source links to `1.0`.

The default workflow action produces an artifact without a GitHub release or tag.
Pushing a source or documentation change does not run the full Dreamcast build.
Firmware writes and every possible hardware combination are not requirements
for this candidate test; the documented limits must remain visible.
