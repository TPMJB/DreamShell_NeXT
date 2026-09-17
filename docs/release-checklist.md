# K-UI 1.0 release checklist

Release version **1.0**, tag **k-ui-1.0**, archive **K-UI-v1.0.zip**.
Historical `1.0` and `v1.0.0` tags must not be moved or reused.

## Completed evidence

- [x] Integrated app/core/loader changes and fixed bootloader 3.3.
- [x] Complete build of tested baseline `62a5fbce9e0af69a48f726cd62f77147491d0c44`:
  [successful run](https://github.com/TPMJB/DreamShell_NeXT/actions/runs/35180819886).
  Includes host checks, storage interoperability, SH-4 compilation/linking,
  runtime imports, full package validation and both CDI checks.
- [x] Maintainer confirms the fixed disc reaches the recovery menu and boots
  after SD insertion. The working disc can be reused for SD updates.
- [x] Maintainer approves full release after reporting the latest app retest
  working apart from an intermittent, unreproduced disc-dumping crash.
- [x] Record that crash and the limits of the ten successful resume/CRC checks.
- [x] Update release notes, installation/build instructions, K-UI presentation,
  visible shell/save labels and announcement drafts.
- [x] Retain upstream and third-party licenses, required notices, and compatible
  internal identifiers, settings and saved-rip formats.

Detailed firmware-write tests, every game/device combination and arcade
peripherals were not individually reported. Do not infer blanket coverage from
the maintainer's general app retest. See [compatibility](compatibility.md).

## Publication

- [ ] Merge the release preparation into `master` after its checks pass.
- [ ] Run **Full K-UI release** from **master** with **Publish a GitHub release**
  selected. Merging alone does not trigger a full build or create a release.
- [ ] Verify that `k-ui-1.0` is public, with `K-UI-v1.0.zip` and `SHA256SUMS`,
  and that its notes identify the final source commit and successful build run.
- [ ] Publish the [Reddit/forum drafts](launch/1.0/README.md) after the download
  exists. Keep the known issue and credits; use real console media where available.

The repository URL stays `TPMJB/DreamShell_NeXT` for continuity. A repository
rename is not a release gate. Publication refuses an existing release tag and a
source branch that changes during the build. The workflow's default remains
an unpublished test artifact.
