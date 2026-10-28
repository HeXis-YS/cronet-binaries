# Cronet Automation

This orphan branch contains the automation that maintains the Chromium fork.
Chromium source lives on versioned `hray/<WebView version>` branches in the
same repository. The release baseline is the Stable WebView entry returned by
ChromiumDash. The release version selects the corresponding Chromium tag.

The weekly workflow currently records the newest WebView release. Automatic
rebase remains pending. The Linux x64 workflow builds a selected source branch
or commit through `workflow_dispatch`.

`hray-core` consumes the stable C ABI from the resulting `libcronet.so`; a
Chromium update does not require an hray-core update when that ABI is unchanged.
The headers and ABI contract live in `components/hray_native` on the source branch.

The Linux x64 ABI 1 baseline passed [build 40666748742](https://github.com/HeXis-YS/cronet-binaries/actions/runs/40666748742)
with Chromium source `85321e2490f5eb9f9cb6a08cc0b92ff08d3d0568` and automation
`69fdc933b3ccc2b56171b1f1a654ab0858475cbf`. Its library exports only the eight
`hray_*` C functions under `HRAY_CRONET_1.0`. The C smoke check covers linkage,
engine initialization, rejected starts and cancellation; successful H2/H3
transfers were not checked. Linux runtime dependencies include NSS/NSPR;
the artifact's `linkage.txt` records the complete resolved dependency list.

For a Linux x64 build, install Python 3 (with `requests`), curl, xz, unzip, Ninja,
pkg-config and NSS development libraries. From this automation checkout, run:

```sh
bash tools/prepare-source.sh /path/to/chromium-fork /path/to/new-build-tree
bash tools/build-linux.sh /path/to/new-build-tree
```

Source preparation downloads the matching official release archive, which
includes Chromium's DEPS sources, then copies the fork's `net` and
`components/hray_native` source trees over it. The fork checkout only needs those
directories and `chrome/VERSION`. No hand-maintained patch series is used.
The build downloads the matching upstream Clang, Rust, GN and Linux sysroot.
It builds the library with ThinLTO and runs a C ABI smoke check. `JOBS` controls
Ninja parallelism (defaults to the available CPU count from `nproc`).
Allow space for a full Chromium release and build
output; run full builds in CI when local resources are limited.

Output is under `out/Release/cronet`: `libcronet.so`, its C headers, WebView
version, source and automation revisions, dynamic exports and linkage details.
The workflow also uploads preparation/build logs and GN arguments.
