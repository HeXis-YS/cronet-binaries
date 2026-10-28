#!/usr/bin/env bash
# Usage: tools/build-linux.sh PREPARED_BUILD_DIRECTORY
# Requires Linux x64, Python 3, curl, unzip, Ninja, pkg-config and libnss3-dev.
set -euo pipefail
automation_dir=$(cd "$(dirname "$0")/.." && pwd)
cd "${1:?prepared Chromium directory required}"

python3 tools/clang/scripts/update.py
python3 tools/rust/update_rust.py
python3 build/linux/sysroot_scripts/install-sysroot.py --arch=amd64
gn_version=$(sed -n "s/.*'gn_version': '\([^']*\)'.*/\1/p" DEPS)
mkdir -p gn
curl -fL "https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/$gn_version" -o gn/gn.zip
unzip -o gn/gn.zip -d gn
rm gn/gn.zip

gn/gn gen out/Release \
  --root-target=//components/hray_native:cronet \
  --root-pattern=//components/hray_native:cronet \
  --script-executable=python3 \
  --args='target_os="linux" target_cpu="x64"
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
nm -D --defined-only out/Release/libcronet.so | tee out/Release/cronet/exports.txt
ldd -r out/Release/libcronet.so | tee out/Release/cronet/linkage.txt
cc -std=c11 -Wall -Wextra -Werror -pthread \
  "$automation_dir/tools/abi-smoke.c" -I components/hray_native/include \
  -L out/Release -Wl,-rpath,'$ORIGIN' -lcronet -o out/Release/abi-smoke
timeout 30 out/Release/abi-smoke
