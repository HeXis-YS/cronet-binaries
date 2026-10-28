# cronet-binaries

## Xray native transport branch

The three `Build Cronet` GitHub Actions workflows independently build Linux x64,
Windows x64, and Android arm64 artifacts with production ThinLTO. Start the
desired platform with `workflow_dispatch`; publishing a release starts all three.
Each run prepares and patches its own Chromium source tree. Runs have no shared
concurrency group, so a new request does not cancel an older request. Each job
uploads its logs, revision, patch series and GN arguments. The upstream-source
migration is validated by CI; no successful build of that new baseline has been
established yet.

This branch builds a shared library for Xray's HTTP/2 and HTTP/3 bidirectional
streams on Linux, Windows, and Android. Its C ABI is
`src/components/xray_native/include/xray_engine.h` plus
`xray_stream.h`; it does not provide the full Cronet engine C API.
The engine requires both ordinary CA/hostname validation and a leaf SPKI SHA-256
pin. Xray supplies fixed isolation groups; Chromium manages their connections.
The stream bridge directly implements Chromium's bidirectional-stream delegate;
it has no gRPC dependency. Its six `xray_stream_*` operations cover creation,
start, read, write, cancellation, and destruction. One read and one write may be
in flight, and a single terminal callback follows transport teardown.

The new baseline retains upstream ICU and Rust implementations to reduce patch
maintenance. Source migration and GN validation are in progress; the previously
installed source tree still contains the older trimming patches. Complete the
source preparation below before using the new build flags.
Certificate validation uses the Chrome root store with platform
system roots; support for separately installed user or enterprise CAs is outside
the delivery requirements. Android is consumed by hray-core through the native
C ABI, without a Java-facing API.

### Upgrade Chromium

Requires Bash, curl, tar with xz support, Git, network access, and enough disk
space for the complete release. From the repository root, choose a release and
a new output directory outside `src`:

```sh
bash tools/prepare-source.sh VERSION OUTPUT_DIRECTORY
```

The script streams the complete release through `curl | tar`, applies
`tools/patches/series` in order, and copies the local build scripts and native
adapter into the output. Any failed command stops preparation; use the output
only after the script succeeds. Build from that directory using the commands
below. Existing `src` files and toolchain caches are left untouched. When
upgrading the pinned release, update `CHROMIUM_VERSION` explicitly.
Each platform workflow moves its prepared source tree into `src` before
restoring toolchain caches; no source artifact is repackaged or exchanged.
The patches retain NaiveProxy authorship and include the Xray isolation/authority
adapters. The old `import-upstream.sh` rewrites history and is not this workflow.
GN selects `components/xray_native`
with `--root-target` and `--root-pattern`; upstream build descriptions and source
files remain intact. The stream ABI is internal: deploy hray-core and its matching
native library together. Patch categories and the migration status are recorded
in [PATCH_REVIEW.md](PATCH_REVIEW.md).

### Build

Install Ninja, pkg-config (Linux), Python, curl, xz, and unzip. Linux and Android
use Linux runners. Windows builds natively on `windows-2025-vs2026` with its
installed Visual Studio 2026 and Windows SDK; `DEPOT_TOOLS_WIN_TOOLCHAIN=0`
selects that installation instead of Google's toolchain download.
For Windows source preparation under Git Bash, enable Windows Developer Mode
and set `MSYS=winsymlinks:nativestrict` before starting Bash. This creates native
symlinks instead of copying their targets, which may not yet exist while the
archive is being extracted. The Windows workflow configures both settings.
Repository `.gitattributes` keeps shell scripts, the Chromium version, patch
series, and patch files in LF format even when Git uses `core.autocrlf=true`.
From `src`, run:

```sh
./get-clang.sh
./build.sh
```

`get-clang.sh` also runs Chromium's Rust toolchain updater. For Android, it records
the installed native toolchain in `build/config/gclient_args.gni` by setting
`checkout_android=true`. This is checkout metadata, not a GN command-line argument;
`build_with_chromium` comes from the release archive's metadata as well.
`./build.sh configure`
generates the library dependency graph without compiling. Production ThinLTO is
enabled by default and its link optimization is configured in the local library
target; use `THIN_LTO=false` explicitly for a build without LTO.

Set `EXTRA_FLAGS='target_cpu="arm64" target_os="android"'` for Android, or use
`target_cpu="x64"` with `target_os="linux"` or `target_os="win"` as in
`.github/workflows/build-{linux,windows,android}.yml`. Run both
commands with the same flags. `JOBS` controls Ninja parallelism (default 2).
Artifacts and matching headers are placed in `src/out/Release/cronet/`.
Set Xray's `XRAY_CRONET_LIBRARY` to the absolute library path.
For an interface build failure, `./build.sh release NINJA_TARGET` builds only
that target and its prerequisites without packaging a library.

The new baseline leaves `protoc` generator selection at upstream defaults;
removing unused generators would add another patch to maintain.

The upstream project description below describes the original v119 distribution;
its full Cronet API and release automation do not describe this branch's output.

> :warning: This project is still under construction. Changes may be force pushed so use at your own risk.

Pre-built, cross-platform binaries for [cronet](https://chromium.googlesource.com/chromium/src/+/master/components/cronet/README.md) (Chromium's networking stack). New cronet binaries will be published to [releases](https://github.com/sleeyax/cronet-binaries/releases) as soon as new stable versions of Chromium are released.

This repository is based on [NaiveProxy](https://github.com/klzgrad/naiveproxy)'s excellent build system, but doesn't include any Naive binaries, tests or go files. The last release of Naive that came with standalone cronet binaries was [v106.0.5249.91-3](https://github.com/klzgrad/naiveproxy/releases/tag/v106.0.5249.91-3) (Nov 2, 2022). Since then, new builds have been [discontinued](https://github.com/klzgrad/naiveproxy/issues/471#issuecomment-1457323770) so this project aims to fill that gap.

## Applied patches
Patches are applied to the Chromium source code to make it compile on all supported platforms and support additional features. The following noteworthy patches are applied:

- Minimize source code and build size
- Disable exceptions and RTTI, except on Mac and Android.
- Support [OpenWrt](https://openwrt.org/) builds
- Use the builtin verifier instead of the system verifier (drop dependency of NSS on Linux) and read the system trust store (following Go's behavior in `crypto/x509/root_unix.go` and `crypto/x509/root_linux.go`):
- Handle AIA response in PKCS#7 format
- Allow higher socket limits for proxies
- Force tunneling for all sockets
- Support HTTP/2 and HTTP/3 CONNECT tunnel Fast Open using the fastopen header
- Pad RST_STREAM frames
- Disable Android JNI
- Use fixed proxy resolution from experimental option proxy_server

Generally, we try to apply [changes from Naive](https://github.com/klzgrad/naiveproxy#changes-from-upstream) that are still relevant and don't break the build. If you have any suggestions, please open an issue.

## Related projects
- [klzgrad/NaiveProxy](https://github.com/klzgrad/naiveproxy) Proxy that uses cronet to camouflage traffic.
- [sleeyax/cronet-go](https://github.com/sleeyax/cronet-go) Go bindings for cronet. This is a fork and continuation of Naive's [sagernet/cronet-go](https://github.com/sagernet/cronet-go).
- [sleeyax/cronet-rs](https://github.com/sleeyax/cronet-rs) Rust bindings for cronet.
- [sleeyax/CronetSharp](https://github.com/sleeyax/CronetSharp) C# bindings for cronet.

## Developers
### Local development
To build cronet locally using [docker](https://www.docker.com/), you can use a combination of [act](https://github.com/nektos/act) and [act-cache-server](https://github.com/sp-ricard-valverde/github-act-cache-server).

```bash
$ act --env ACTIONS_CACHE_URL=http://localhost:<PORT>/ --env ACTIONS_RUNTIME_URL=http://localhost:<PORT>/ --env ACTIONS_RUNTIME_TOKEN=<TOKEN> --job linux
```

Replace `<PORT>` and `<TOKEN>` with the port and token you configured for your local `act-cache-server` instance. Using a cache server is optional, but will speed up the build process significantly.
