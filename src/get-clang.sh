#!/bin/sh
set -ex

. ./get-sysroot.sh

if [ "$SYSROOT_ARCH" -a ! -d ./"$WITH_SYSROOT/lib" ]; then
  ./build/linux/sysroot_scripts/sysroot_creator.py build "$SYSROOT_ARCH"
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

if [ "$target_os" = android -a ! -f third_party/llvm-build/Release+Asserts/.android-runtime ]; then
  clang_path="clang-android-runtime-library-$CLANG_REVISION.tar.xz"
  curl --fail "https://commondatastorage.googleapis.com/chromium-browser-clang/$WITH_CLANG/$clang_path" |
    tar xJf - -C third_party/llvm-build/Release+Asserts
  touch third_party/llvm-build/Release+Asserts/.android-runtime
fi

# sccache
if [ "$host_os" = win -a ! -f ~/.cargo/bin/sccache.exe ]; then
  sccache_url="https://github.com/mozilla/sccache/releases/download/0.2.12/sccache-0.2.12-x86_64-pc-windows-msvc.tar.gz"
  mkdir -p ~/.cargo/bin
  curl -L "$sccache_url" | tar xzf - --strip=1 -C ~/.cargo/bin
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

if [ "$target_os" = android -a ! -d third_party/android_toolchain/ndk ]; then
  android_ndk_version=r24
  curl -LO https://dl.google.com/android/repository/android-ndk-$android_ndk_version-linux.zip
  unzip android-ndk-$android_ndk_version-linux.zip
  mkdir -p third_party/android_toolchain/ndk
  cd android-ndk-$android_ndk_version
  cp -r --parents sources/android/cpufeatures ../third_party/android_toolchain/ndk
  cp -r --parents toolchains/llvm/prebuilt ../third_party/android_toolchain/ndk
  cd ..
  cd third_party/android_toolchain/ndk
  find toolchains -type f -regextype egrep \! -regex \
    '.*(lib(atomic|gcc|gcc_real|compiler_rt-extras|android_support|unwind).a|crt.*o|lib(android|c|dl|log|m).so|usr/local.*|usr/include.*)' -delete
  # Works around https://github.com/android/ndk/issues/2082
  # .../android/hardware_buffer.h:322:42: error: expression is not an integral constant expression
  #   322 |     AHARDWAREBUFFER_USAGE_FRONT_BUFFER = 1UL << 32,
  # .../android/hardware_buffer.h:322:46: note: shift count 32 >= width of type 'unsigned long' (32 bits)
  #   322 |     AHARDWAREBUFFER_USAGE_FRONT_BUFFER = 1UL << 32,
  sed -i 's/AHARDWAREBUFFER_USAGE_FRONT_BUFFER = 1UL /AHARDWAREBUFFER_USAGE_FRONT_BUFFER = 1ULL /' toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android/hardware_buffer.h
  cd -
  rm -rf android-ndk-$android_ndk_version android-ndk-$android_ndk_version-linux.zip
fi
