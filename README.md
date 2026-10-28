# cronet-binaries

## Xray native transport branch

The three `Build Cronet` GitHub Actions workflows independently build Linux x64,
Windows x64, and Android arm64 artifacts with production ThinLTO. Start the
desired platform with `workflow_dispatch`; publishing a release starts all three.
Each run prepares and patches its own Chromium source tree. Runs have no shared
concurrency group, so a new request does not cancel an older request. Each job
uploads its logs, revision, patch series and GN arguments. The rewritten patch
series still requires complete platform builds and runtime validation in CI.

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

The baseline retains upstream ICU, Rust, JNI generators and Android build
templates. The checked-in Chromium snapshot still contains older patches;
prepare a clean source tree using the command below before building.
Certificate validation follows upstream platform defaults; this project does
not customize CA sources. The leaf SPKI pin remains an additional check.
Android is consumed by hray-core through the native
C ABI without initializing a JVM. Java tools are used during the build to
generate upstream JNI headers; this does not introduce a runtime JVM requirement.

### Native patch responsibilities

The previous 12 active patches have been replaced from Chromium 156.0.8078.47.
Only the entries in `tools/patches/series` are applied:

| Patch | Responsibility |
| --- | --- |
| `01-native-request-context.patch` | Carry the isolation key into H2/H3 session selection and derive the anonymization key from it. Carry HTTP authority separately from the DNS/TLS URL. |
| `02-native-socket-limits.patch` | Set native pool/group/proxy limits together (2048/2040/2048), with bounds checks; leave upstream browser policy setters intact. |
| `04-android-native-runtime.patch` | Use Android properties for device/build information; handle absent APK/activity metadata and optional Java network services without attaching a JVM. Preserve native scheduling, socket binding and DNS APIs. |
| `05-windows-oom-declaration.patch` | Keep the upstream Windows OOM exception code declared when PartitionAlloc is disabled. |
| `06-android-build-config-dependencies.patch` | Declare the Java targets whose GN-time parameter files are consumed by Android build-config actions. |

The former file-backed trust patch has been removed. Linux uses upstream NSS
integration; Windows and Android retain upstream platform trust integration.
Android's Java-backed trust path remains an open dependency for a no-JVM core;
patch 04 does not resolve it. CA/hostname checks and the leaf SPKI pin remain
required. A matching pin does not bypass ordinary certificate validation.
Without Java, Android uses the system resolver when Chromium cannot obtain a
Java DNS configuration. Activity notifications, TrafficStats tags and Java QUIC
socket registration are unavailable. Native network binding is retained.
The [Android native evaluation](docs/android-native-evaluation.md) assesses a
simpler implementation for SimpleXray's separate core process; it is not an
implemented replacement for patch 04.

The old Java-template suppression, JNI stubs, `-latomic` removal, Windows SDK
downgrade and custom Debian sysroot builder are no longer applied.
`PATCH_REVIEW.md` remains the frozen **previous** design baseline; its old patch
table is historical. This section and `series` describe the current build.

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
The old `import-upstream.sh` rewrites history and is not this workflow.
GN starts from `components/xray_native` with `--root-target` and limits generation
to `cronet` and its dependencies with `--root-pattern`; upstream build descriptions
and source files outside the listed patches remain intact. The
stream ABI is internal:
deploy hray-core and its matching native library together.

To verify patch application without downloading or compiling full Chromium:

```sh
python3 tools/verify-patches.py .codex/runs/patch-check-01
```

Use a new output directory each time. The verifier downloads only touched files
from the pinned upstream tag and saves their hashes, applied sources, conditions
and logs. This check does not replace a complete GN/build or runtime test.

### Build

Install Ninja, pkg-config and NSS development libraries (Linux), Python, curl,
xz, and unzip. Linux and Android
use Linux runners. Windows builds natively on `windows-2025-vs2026` with its
installed Visual Studio 2026. `tools/install-windows-sdk.ps1` installs the required
Windows SDK 10.0.28000.0 before configuration; `DEPOT_TOOLS_WIN_TOOLCHAIN=0`
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

`get-clang.sh` runs Chromium's Rust updater and prebuilt Linux sysroot installer.
Android NDK, JDK and SDK packages are selected from the prepared release's DEPS
by `install-android-tools.py` (`--list` prints the selected packages). The script
checks existing files and tool versions before installation, including tools
exposed through `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK*` and
`JAVA_HOME*`. Matching tools are copied into the Chromium layout for caching;
missing or incompatible tools are downloaded. A version stamp alone does not
skip validation. The SDK platform's opaque CIPD revision must also match its
stamp; an API level alone does not establish that revision.
`python3 install-android-tools.py --check` checks availability without downloading
or copying, and exits with status 1 if any required package is unavailable.
Installation validates the files and commands again before recording a stamp.
Installer regression checks run from the repository root with
`python3 -m unittest discover -s tools/tests -p 'test_install_android_tools.py' -v`.
The script installs complete packages, preserving NDK libraries and headers. For Android,
`get-clang.sh` records the installed toolchain in `build/config/gclient_args.gni` by setting
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

## Original upstream distribution patches (historical)
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
