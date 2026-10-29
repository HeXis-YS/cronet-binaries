#!/usr/bin/env bash
# Usage: tools/prepare-source.sh CHROMIUM_WORKSPACE/src [linux|android]
# Requires a full fork checkout in a dedicated workspace, Git and Python 3.
set -euo pipefail
source_dir=$(realpath "${1:?Chromium checkout named src required}")
target_os=${2:-linux}
case "$target_os" in
  linux|android) ;;
  *) echo "Unsupported target OS: $target_os" >&2; exit 1 ;;
esac
if [[ $(basename "$source_dir") != src ]]; then
  echo 'Chromium checkout must be named src: DEPS paths are rooted at src/.' >&2
  exit 1
fi

# Bootstrap the exact depot_tools revision tracked by this fork commit.
git -C "$source_dir" submodule update --init --depth 1 third_party/depot_tools
export PATH="$source_dir/third_party/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0
cd "$source_dir/.."
# Leave the selected fork commit untouched; sync only its DEPS dependencies.
gclient config --name src --unmanaged "$(git -C "$source_dir" remote get-url origin)"
if [[ $target_os == android ]]; then
  # gclient derives checkout_android and generates gclient_args.gni from this.
  printf '\ntarget_os = ["android"]\n' >> .gclient
fi
gclient sync --no-history

version=$(cut -d= -f2 "$source_dir/chrome/VERSION" | paste -sd.)
printf '%s\n' "$version" > "$source_dir/WEBVIEW_VERSION"
git -C "$source_dir" rev-parse HEAD > "$source_dir/SOURCE_REVISION"
