#!/bin/sh
set -ex

. ./get-sysroot.sh

if [ "$SYSROOT_ARCH" -a ! -d ./"$WITH_SYSROOT/lib" ]; then
  "$PYTHON" build/linux/sysroot_scripts/install-sysroot.py --arch="$SYSROOT_ARCH"
fi

if [ "$OPENWRT_FLAGS" ]; then
  ./get-openwrt.sh
fi

# Clang
# See src/tools/clang/scripts/update.py
case "$host_os" in
  linux) WITH_CLANG=Linux_x64;;
  win) WITH_CLANG=Win;;
  mac) WITH_CLANG=Mac;;
esac
if [ "$host_os" = mac -a "$host_cpu" = arm64 ]; then
  WITH_CLANG=Mac_arm64
fi
mkdir -p third_party/llvm-build/Release+Asserts
cd tools/clang/scripts
CLANG_REVISION=$($PYTHON -c 'import update; print(update.PACKAGE_VERSION)')
cd -
installed_revision=$(cat third_party/llvm-build/Release+Asserts/cr_build_revision 2>/dev/null || true)
if [ "$installed_revision" != "$CLANG_REVISION" ] || [ ! -f third_party/llvm-build/Release+Asserts/bin/clang ]; then
  mkdir -p third_party/llvm-build/Release+Asserts
  clang_path="clang-$CLANG_REVISION.tar.xz"
  clang_url="https://commondatastorage.googleapis.com/chromium-browser-clang/$WITH_CLANG/$clang_path"
  curl --fail "$clang_url" | tar xJf - -C third_party/llvm-build/Release+Asserts
  echo "$CLANG_REVISION" >third_party/llvm-build/Release+Asserts/cr_build_revision
  rm -f third_party/llvm-build/Release+Asserts/.android-runtime
fi

# Use Chromium's matching Rust package instead of maintaining C++ replacements
# for upstream Rust components. The updater checks the installed version.
$PYTHON tools/rust/update_rust.py

if [ "$target_os" = android -a ! -f third_party/llvm-build/Release+Asserts/.android-runtime ]; then
  clang_path="clang-android-runtime-library-$CLANG_REVISION.tar.xz"
  curl --fail "https://commondatastorage.googleapis.com/chromium-browser-clang/$WITH_CLANG/$clang_path" |
    tar xJf - -C third_party/llvm-build/Release+Asserts
  touch third_party/llvm-build/Release+Asserts/.android-runtime
fi

# GN
# See src/DEPS
case "$host_os" in
  linux) WITH_GN=linux-amd64;;
  win) WITH_GN=windows-amd64;;
  mac) WITH_GN=mac-amd64;;
esac
if [ "$host_os" = mac -a "$host_cpu" = arm64 ]; then
  WITH_GN=mac-arm64
fi
gn_version=$(grep "'gn_version':" DEPS | cut -d"'" -f4)
installed_gn=$(cat gn/out/revision 2>/dev/null || true)
if [ ! -f gn/out/gn ] || [ "$installed_gn" != "$gn_version" ]; then
  mkdir -p gn/out
  curl --fail -L "https://chrome-infra-packages.appspot.com/dl/gn/gn/$WITH_GN/+/$gn_version" -o gn.zip
  unzip -o gn.zip -d gn/out
  rm gn.zip
  echo "$gn_version" > gn/out/revision
fi

if [ "$target_os" = android ]; then
  "$PYTHON" install-android-tools.py
  # The release archive records a desktop checkout. After installing the native
  # Android tools, update its metadata; checkout_android is not a GN build arg.
  sed -i 's/^checkout_android = false$/checkout_android = true/' build/config/gclient_args.gni
fi
