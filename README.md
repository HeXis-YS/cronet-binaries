# cronet-binaries ![build workflow](https://github.com/sleeyax/cronet-binaries/actions/workflows/build.yml/badge.svg)

## Xray native transport branch

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

The XHTTP build omits ICU. Supply ASCII hostnames or punycode for international
domain names. Certificate validation uses the Chrome root store with platform
system roots; support for separately installed user or enterprise CAs is outside
the delivery requirements. Android is consumed by hray-core through the native
C ABI, without a Java-facing API.

### Upgrade Chromium

Requires Python 3.12+, Git, network access, and enough disk space for both the
staged and installed sources. From the repository root, choose a release and a
new staging directory outside `src`:

```sh
python3 tools/stage-upstream.py VERSION STAGE_DIRECTORY
python3 tools/apply-upstream-patches.py STAGE_DIRECTORY
python3 tools/install-upstream.py STAGE_DIRECTORY
```

Wait for staging to finish before applying patches. A patch conflict stops the
upgrade and must be resolved for that release before installation. The ordered
patches in `tools/patches/series` retain NaiveProxy authorship and include the
Xray isolation/authority adapters. Installation updates `CHROMIUM_VERSION`,
preserves local build scripts and the stream adapter, and does not rewrite Git
history. The old `import-upstream.sh` rewrites history and is not this workflow.
The local `src/BUILD.gn` entry point and `components/xray_native` are preserved
across upgrades. The stream ABI is internal: deploy hray-core and its matching
native library together.

### Build

Install Ninja, pkg-config (Linux), Python, curl, xz, and unzip. Windows also needs
Visual Studio with its C++ toolchain and Windows SDK; Android is built on Linux.
From `src`, run:

```sh
./get-clang.sh
./build.sh
```

Set `EXTRA_FLAGS='target_cpu="arm64" target_os="android"'` for Android or choose
`linux`/`win` and a supported CPU from `.github/workflows/build.yml`. Run both
commands with the same flags. `JOBS` controls Ninja parallelism (default 2).
Artifacts and matching headers are placed in `src/out/Release/cronet/`.
Set Xray's `XRAY_CRONET_LIBRARY` to the absolute library path.
For an interface build failure, `./build.sh release NINJA_TARGET` builds only
that target and its prerequisites without packaging a library.

The host `protoc` builds only C++ and Python generators. Python output is used
by Chromium's current proto build actions; Java generation is unused by this
native library, including Android builds, and is removed by patch 41.

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
