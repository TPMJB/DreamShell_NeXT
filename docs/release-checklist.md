# K-UI 1.0.1 release checklist

Release version **1.0.1**, tag **k-ui-1.0.1**, archive **K-UI-v1.0.1.zip**.
Existing tags, including `k-ui-1.0`, `1.0` and `v1.0.0`, must not be moved or reused.

## Completed evidence

- [x] Integrated app/core/loader changes and fixed bootloader 3.3.
- [x] Complete build of tested baseline `62a5fbce9e0af69a48f726cd62f77147491d0c44`:
  [successful run](https://github.com/TPMJB/K-UI_DS/actions/runs/35180819886).
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
- [x] Merge [PR #15](https://github.com/TPMJB/K-UI_DS/pull/15), including the core
  cache fix and GD Ripper 2.2.5 finalization diagnostics.
- [x] Record the E.G.G. console retest with music enabled: full catalog match,
  zero sector-check failures and completed finalization. This single run does
  not prove every intermittent crash is resolved.

Detailed firmware-write tests, every game/device combination and arcade
peripherals were not individually reported. Do not infer blanket coverage from
the maintainer's general app retest. See [compatibility](compatibility.md).

## Publication

- [x] Prepare 1.0.1 version metadata and release notes for `master`; packaged
  and public release-note rendering was checked. Runtime code is the tested PR #15.
- [ ] Run **Full K-UI release** from **master** with **Publish a GitHub release**
  selected. Merging alone does not trigger a full build or create a release.
- [ ] Verify that `k-ui-1.0.1` is public, with `K-UI-v1.0.1.zip` and `SHA256SUMS`,
  and that its notes identify the final source commit and successful build run.
- [ ] Update the existing Reddit/forum announcement after the download exists.
  Describe the targeted fix and E.G.G. result without claiming universal crash
  resolution. The [1.0 launch kit](launch/1.0/README.md) remains available.

The current repository is `TPMJB/K-UI_DS`. Publication refuses an existing release tag and a
source branch that changes during the build. The workflow's default remains
an unpublished test artifact.
