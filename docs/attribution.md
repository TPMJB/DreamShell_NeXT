# K-UI attribution and build records

K-UI enhancements and branding are credited to **TPMJB**. The root `NOTICE`
includes this attribution alongside the original DreamShell notices for
**Ruslan Rostovtsev (SWAT)** and the existing third-party credits. This notice
does not claim authorship of the upstream project or its other contributors' work.

K-UI 1.0 builds contain a readable `next-build.json` record inside the core's
ROM disk, accessible as `/rd/next-build.json`. A copy is installed at
`DS/doc/next-build.json`, and the complete release's `build-info.json` includes
the record under `provenance`. The record identifies K-UI, TPMJB, the repository,
project version, full source commit, and SHA-256 of the distributed `NOTICE`.
`tracked_changes` reports modifications to tracked source files at build time;
it does not inventory untracked files. Source archives without Git metadata
remain buildable and record `source_commit: "unknown"` and `tracked_changes: null`.

The Makefile refreshes the record before building the core's ROM disk and
refreshes the installed notices even during incremental builds. To inspect the
generated record locally, run:

```sh
python3 utils/build_provenance.py
cat romdisk/next-build.json
```

The official K-UI release packager explicitly rejects missing contributor
notices, changed or missing packaged licensing documents, stale build identities,
and cores whose embedded record differs from the packaged record. Existing
checks still validate the boot badge and core logo against the source assets.
`resources/next-branding.sha256` also pins the approved boot badge and core logo,
so editing or removing those assets produces an explicit packaging error.
For an intentional artwork update, review the new artwork and update that
manifest with `sha256sum resources/boot-disc-badge.mr romdisk/logo.kmg.gz`.
Failures identify the missing or mismatched item and stop packaging.

The record is inert data. There are no startup checks, delayed failures, altered
reads or writes, or dependencies between branding and program behavior. Custom
builds can change branding and packaging policy explicitly. The identifier is
not a signature, access restriction, or proof that a binary has not been modified;
someone with the source can change or remove it. Published release hashes and
commit history provide additional comparison evidence.

These additions first enter the complete K-UI package with 1.0. Published
0.9.1 binaries, tags, and release assets remain unchanged.
