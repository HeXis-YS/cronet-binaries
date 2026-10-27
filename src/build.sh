#!/bin/sh
# Build the Xray bidirectional-stream library. Run get-clang.sh first.
# EXTRA_FLAGS selects target_cpu/target_os as in NaiveProxy.
# ./build.sh configure only generates the target graph, without compiling.
set -e
cd "$(dirname "$0")"
. ./get-sysroot.sh
out=out/Release
flags='is_debug=false is_official_build=true symbol_level=0'
if [ "${1:-}" = debug ]; then
  out=out/Debug
  flags='is_debug=true is_official_build=false symbol_level=1'
fi
flags="$flags
  is_component_build=false
  is_clang=true
  is_cronet_build=true
  chrome_pgo_phase=0
  use_thin_lto=${THIN_LTO:-true}
  thin_lto_enable_optimizations=true
  is_cfi=false
  use_sysroot=false
  use_allocator_shim=false
  use_partition_alloc=false
  enable_backup_ref_ptr_support=false
  enable_dangling_raw_ptr_checks=false
  use_clang_modules=false
  use_udev=false
  use_aura=false
  use_ozone=false
  use_gio=false
  use_glib=false
  use_platform_icu_alternatives=false
  enable_rust=true
  enable_chromium_prelude=true
  chrome_root_store_optional=false
  chrome_root_store_only=true
  is_perfetto_embedder=true
  disable_file_support=true
  enable_websockets=false
  use_kerberos=false
  enable_mdns=false
  enable_reporting=false
  include_transport_security_state_preload_list=false
  enable_device_bound_sessions=false
  enable_disk_cache_sql_backend=false
  use_nss_certs=false
  use_nss_client_certs=false
  use_nss_server_certs=false
"
if [ "${WITH_SYSROOT:-}" ]; then
  flags="$flags target_sysroot=\"//$WITH_SYSROOT\""
fi
if [ "$target_os" = android ]; then
  flags="$flags is_desktop_android=true checkout_android=true default_min_sdk_version=24"
fi
if [ "$target_os" = win ] && [ "$host_os" = linux ]; then
  export DEPOT_TOOLS_WIN_TOOLCHAIN=1
else
  export DEPOT_TOOLS_WIN_TOOLCHAIN=0
fi
./gn/out/gn gen "$out" \
  --root-target=//components/xray_native:cronet \
  --root-pattern=//components/xray_native:cronet \
  --script-executable=python3 \
  --args="$flags ${EXTRA_FLAGS:-}"
if [ "${1:-}" = configure ]; then
  exit
fi
if [ "${2:-}" ]; then
  # Compile a specific target or object when resolving an interface failure.
  ninja -C "$out" -j "${JOBS:-2}" "$2"
  exit
fi
ninja -C "$out" -j "${JOBS:-2}" components/xray_native:cronet
./bundle-binaries.sh "$out"
