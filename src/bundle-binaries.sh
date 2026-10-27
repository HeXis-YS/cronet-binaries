#!/bin/sh
# Package the runtime library and the matching C ABI headers.
set -e
cd "$(dirname "$0")"
. ./get-sysroot.sh
out=${1:-out/Release}
case "$target_os" in
  win) library=cronet.dll ;;
  linux|android) library=libcronet.so ;;
  *) echo "Unsupported Cronet target: $target_os" >&2; exit 1 ;;
esac
mkdir -p "$out/cronet"
cp "$out/$library" "$out/cronet/"
cp components/xray_native/include/xray_stream.h "$out/cronet/"
cp components/xray_native/include/xray_engine.h "$out/cronet/"
cp ../CHROMIUM_VERSION "$out/cronet/"
