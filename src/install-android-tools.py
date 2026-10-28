#!/usr/bin/env python3
"""Install native Android and JNI generation tools pinned by Chromium DEPS.

Run from a prepared source tree on Linux x64. Requires curl and unzip.
--list prints the selected packages without downloading them.
"""

import argparse
import json
from pathlib import Path
import platform
import subprocess
import urllib.parse


def packages(root):
    # DEPS is trusted executable source shipped with the Chromium release,
    # just like its toolchain update scripts. Preserve its Var/Str semantics.
    scope = {"Str": str}
    scope["Var"] = lambda name: scope["vars"][name]
    exec(compile((root / "DEPS").read_text(), "DEPS", "exec"), scope)
    for key in (
        "src/third_party/android_toolchain/ndk",
        "src/third_party/jdk/current",
        "src/third_party/android_sdk/public",
    ):
        for package in scope["deps"][key]["packages"]:
            name = package["package"]
            # A native shared library needs JNI headers, not an emulator or adb.
            if key.endswith("android_sdk/public") and not any(
                part in name for part in ("/build-tools/", "/platforms/")
            ):
                continue
            name = name.replace("${{platform}}", "linux-amd64")
            name = name.replace("${{os}}", "linux")
            yield key.removeprefix("src/"), name, package["version"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    selected = list(packages(root))
    if args.list:
        print(json.dumps(selected, indent=2))
        return
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("Android tool installation requires a Linux x64 host")
    for directory, name, version in selected:
        destination = root / directory
        destination.mkdir(parents=True, exist_ok=True)
        # Each SDK package has its own stamp because they share a directory.
        stamp = destination / ("." + name.replace("/", "_") + ".version")
        if stamp.exists() and stamp.read_text() == version:
            continue
        url = "https://chrome-infra-packages.appspot.com/dl/" + name
        url += "/+/" + urllib.parse.quote(version, safe="")
        archive = destination / ".install-package.zip"
        print(f"Installing {name} {version} into {directory}", flush=True)
        subprocess.run(["curl", "--fail", "--location", "--show-error",
                        url, "-o", str(archive)], check=True)
        # unzip preserves the executable permissions recorded in CIPD archives.
        subprocess.run(["unzip", "-oq", str(archive), "-d", str(destination)],
                       check=True)
        archive.unlink()
        stamp.write_text(version)


if __name__ == "__main__":
    main()
