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

For a Linux x64 build, install Git, Python 3, curl, unzip,
Ninja, pkg-config and NSS development libraries. From this automation checkout,
create a full fork checkout named `src` in a dedicated Chromium workspace:

```sh
git clone --depth 1 --filter=blob:none --branch hray/157.0.8103.14 \
  https://github.com/HeXis-YS/cronet-binaries.git /path/to/chromium/src
bash tools/prepare-source.sh /path/to/chromium/src
bash tools/build-linux.sh /path/to/chromium/src
```

Source preparation initializes the fork's pinned `third_party/depot_tools`
submodule, writes `.gclient` in the workspace, and runs
`gclient sync --no-history` to fetch dependencies from that commit's
`DEPS`. The `src` solution is unmanaged so synchronization keeps the selected
fork commit. Builds use this checkout directly, including changes in any
directory; no official source archive or source overlay is used.
Synchronization installs the pinned Clang, Rust, GN and Linux sysroot and runs
the upstream hooks to generate build metadata. The build script uses these tools
to build the library with ThinLTO and run a C ABI smoke check. `JOBS` controls
Ninja parallelism (defaults to the available CPU count from `nproc`).
Allow space for a full Chromium release and build
output; run full builds in CI when local resources are limited.

Output is under `out/Release/cronet`: `libcronet.so`, its C headers, WebView
version, source and automation revisions, dynamic exports and linkage details.
The workflow also uploads preparation/build logs and GN arguments.
