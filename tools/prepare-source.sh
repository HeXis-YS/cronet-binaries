#!/usr/bin/env bash
# Usage: bash tools/prepare-source.sh VERSION NEW_OUTPUT_DIRECTORY
# Requires Bash, curl, tar with xz support, and Git. Leaves partial output on failure.
set -euo pipefail

if [[ $# != 2 || ! $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Usage: $0 VERSION NEW_OUTPUT_DIRECTORY" >&2
  exit 1
fi
root=$(cd "$(dirname "$0")/.." && pwd)
version=$1
mkdir -p "$(dirname "$2")"
mkdir "$2"
source_dir=$(cd "$2" && pwd)
url="https://commondatastorage.googleapis.com/chromium-browser-official/chromium-$version.tar.xz"
echo "Downloading $url into $source_dir"
curl --fail --location --show-error "$url" |
  tar -xJf - --strip-components=1 -C "$source_dir"
while IFS= read -r patch; do
  echo "Applying $patch"
  git apply --unsafe-paths -p2 --directory="$source_dir" "$root/tools/patches/$patch"
done < "$root/tools/patches/series"
cp -R "$root/src/components/xray_native" "$source_dir/components/"
cp "$root"/src/*.sh "$source_dir/"
cp "$root/src/install-android-tools.py" "$source_dir/"
echo "Prepared Chromium $version in $source_dir"
