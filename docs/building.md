# Building K-UI

Build K-UI **1.0.1** from the reviewed **`master`** branch.
The release tag is **k-ui-1.0.1**. Existing tags, including `k-ui-1.0`, `1.0`
and `v1.0.0`, must not be moved or reused.

## GitHub Actions

1. Open **Actions → Full K-UI release → Run workflow** (`full-release.yml`).
2. Select **master**. For the public release, check **Publish a GitHub release**.
   Leave it unchecked only when you want a test artifact.
3. After success, the release contains **K-UI-v1.0.1.zip** and **SHA256SUMS**.
   The workflow also uploads the `K-UI-v1.0.1` artifact, with the complete `DS`
   folder, both CDI images, guides and host tools.
4. Record the run URL and source commit when reporting console results.

The workflow builds and validates the exact commit, uploads the archive and
checksum as a draft, then publishes **k-ui-1.0.1**. It rejects existing tags and
refuses publication if `master` changed during the build. Publication requires
repository write access. Merging the source alone does not publish the release.

Pushes to `master` and pull requests targeting it run the host checks workflow. They
do not rebuild the toolchain, produce disc images, or publish releases. Historical
app-specific build workflows are preserved in Git history and their original
branches; the default branch uses one complete build workflow.

## Host tests

On Ubuntu 22.04, install the tools used by the suite:

```sh
sudo apt-get install build-essential python3 python3-pil exfatprogs dosfstools
REQUIRE_EXFAT_TESTS=1 python3 -m unittest discover -s utils/tests -v
```

Tests include production C harnesses, address/undefined-behavior sanitizers and
Linux FAT32/exFAT interoperability. A host pass does not emulate the Dreamcast.
Some container hosts restrict LeakSanitizer's process inspection; only on such
hosts, `ASAN_OPTIONS=detect_leaks=0` allows the address/undefined checks to run
without leak detection. The GitHub workflow retains normal sanitizer settings.

## Complete local build

The supported reference environment is Ubuntu 22.04. The workflow is the
authoritative package list and build order. Start in a clean checkout with
submodules initialized. The helper uses `/usr/local/dc/kos` and `/opt/toolchains/dc`;
its `prepare` step recreates `/usr/local/dc/kos/kos` at the pinned revision.

```sh
git clone --recurse-submodules https://github.com/TPMJB/K-UI_DS.git
cd K-UI_DS
git checkout master
# To reproduce the published release once available: git checkout k-ui-1.0.1
```

Install the packages listed in `.github/workflows/full-release.yml`, including
tolua built from LuaDist/tolua. Set up kos-ports alongside the KallistiOS tree:

```sh
sudo mkdir -p /usr/local/dc/kos /opt/toolchains/dc
sudo chown -R "$(id -u):$(id -g)" /usr/local/dc /opt/toolchains/dc
git clone https://github.com/KallistiOS/kos-ports.git /usr/local/dc/kos/kos-ports
export GITHUB_WORKSPACE="$PWD"
bash .github/scripts/dev-build.sh prepare
bash .github/scripts/dev-build.sh toolchain
bash .github/scripts/dev-build.sh loaders
bash .github/scripts/dev-build.sh kos
bash .github/scripts/dev-build.sh ports
bash .github/scripts/dev-build.sh release
python3 utils/package_release.py
```

`prepare` selects the KallistiOS revision in `sdk/doc/KallistiOS.txt` and applies
the three patches in `sdk/kos-patches/`. The build uses the kos-ports checkout
present on the host; Actions resolves its current revision for cache selection.
This is a source/version-tracked build, not a claim of bit-for-bit reproducibility
across arbitrary compilers or dependency updates.

The final output is `K-UI-v1.0.1.zip` plus an outer `SHA256SUMS`.
The packager validates source-matching app XML, SH-4 modules, loader payloads,
branding, required guides and the embedded build record. Do not distribute
`DreamShell-dev.zip` as the sealed release artifact.

For individual changes after the environment is prepared:

```sh
source /usr/local/dc/kos/kos/environ.sh
make                                      # core and libraries
make -C applications/gd_ripper/modules     # GD Ripper module
make clean-all                            # clean generated project outputs
```

Use the [release checklist](release-checklist.md) and
[hardware result record](compatibility.md) before publishing.
