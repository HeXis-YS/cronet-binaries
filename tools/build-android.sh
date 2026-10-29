#!/usr/bin/env bash
# Usage: tools/build-android.sh PREPARED_BUILD_DIRECTORY
# Requires an Android DEPS-synced checkout on Linux x64, Python 3, Ninja and pkg-config.
set -euo pipefail
automation_dir=$(cd "$(dirname "$0")/.." && pwd)
cd "${1:?prepared Chromium directory required}"

# Android's Java metadata uses GN-time write_file() across dependency directories;
# root-pattern pruning leaves inputs of other parsed targets without those files.
buildtools/linux64/gn gen out/Release \
  --root-target=//components/hray_native:cronet \
  --script-executable=python3 \
  --args='target_os="android" target_cpu="arm64" is_cronet_build=true
    is_debug=false is_official_build=true is_component_build=false symbol_level=0
    is_clang=true chrome_pgo_phase=0 use_thin_lto=true thin_lto_enable_optimizations=true
    is_cfi=false use_allocator_shim=false use_partition_alloc=false
    enable_backup_ref_ptr_support=false enable_dangling_raw_ptr_checks=false
    use_clang_modules=false use_udev=false use_aura=false use_ozone=false
    use_gio=false use_glib=false use_platform_icu_alternatives=false
    enable_rust=true enable_chromium_prelude=true is_perfetto_embedder=true
    disable_file_support=true enable_websockets=false use_kerberos=false
    enable_mdns=false enable_reporting=false
    include_transport_security_state_preload_list=false
    enable_device_bound_sessions=false enable_disk_cache_sql_backend=false'
ninja -C out/Release -j "${JOBS:-$(nproc)}" components/hray_native:cronet

mkdir -p out/Release/cronet
cp out/Release/libcronet.so components/hray_native/include/*.h \
  WEBVIEW_VERSION SOURCE_REVISION out/Release/cronet/
git -C "$automation_dir" rev-parse HEAD > out/Release/cronet/AUTOMATION_REVISION
llvm_bin=third_party/llvm-build/Release+Asserts/bin
"$llvm_bin/llvm-readelf" -h -d -V out/Release/libcronet.so | tee out/Release/cronet/linkage.txt
grep -q 'Machine:.*AArch64' out/Release/cronet/linkage.txt
"$llvm_bin/llvm-nm" -D --defined-only out/Release/libcronet.so | tee out/Release/cronet/exports.txt
# Check the same public ABI as Linux without executing an Android library on the host.
diff -u \
  <(sed -n 's/^    \(hray_[a-z_]*\);$/\1/p' components/hray_native/exports.map | LC_ALL=C sort) \
  <(awk '$2 != "A" {sub(/@@.*/, "", $3); print $3}' out/Release/cronet/exports.txt | LC_ALL=C sort)
