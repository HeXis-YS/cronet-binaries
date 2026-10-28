#!/usr/bin/env bash
# Usage: tools/prepare-source.sh FORK_CHECKOUT NEW_BUILD_DIRECTORY
set -euo pipefail
source_dir=$(realpath "${1:?fork checkout required}")
build_dir=$(realpath -m "${2:?new build directory required}")
version=$(cut -d= -f2 "$source_dir/chrome/VERSION" | paste -sd.)
mkdir "$build_dir"
curl -fL "https://commondatastorage.googleapis.com/chromium-browser-official/chromium-$version.tar.xz" |
  tar -xJf - --strip-components=1 -C "$build_dir"
# The release archive includes DEPS sources absent from a plain Chromium clone.
# The fork owns the native adapter and net changes; no patch series is applied.
cp -a "$source_dir/net/." "$build_dir/net/"
cp -a "$source_dir/components/hray_native" "$build_dir/components/"
printf '%s\n' "$version" > "$build_dir/WEBVIEW_VERSION"
git -C "$source_dir" rev-parse HEAD > "$build_dir/SOURCE_REVISION"
