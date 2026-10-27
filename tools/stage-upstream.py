#!/usr/bin/env python3
"""Stage a Chromium release without replacing sources or rewriting Git history.

Usage: python3 tools/stage-upstream.py VERSION OUTPUT_DIRECTORY
The output directory must not exist. Uses tools/include.txt and exclude.txt;
downloads are streamed, so the full Chromium archive is not stored locally.
"""

import argparse
import copy
import fnmatch
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", args.version):
        parser.error("version must contain four numeric components")
    root = Path(__file__).resolve().parent.parent
    includes = (root / "tools/include.txt").read_text().splitlines()
    excludes = (root / "tools/exclude.txt").read_text().splitlines()
    args.output.mkdir(parents=True, exist_ok=False)
    prefix = f"chromium-{args.version}/"
    url = f"https://commondatastorage.googleapis.com/chromium-browser-official/chromium-{args.version}.tar.xz"
    count = 0
    include_regex = re.compile("|".join(fnmatch.translate(pattern.lstrip("^")) for pattern in includes))
    prefixes = tuple(pattern.lstrip("^").rstrip("/") + "/" for pattern in includes if not any(c in pattern for c in "*?["))
    exclude_regex = re.compile("|".join(fnmatch.translate(pattern) for pattern in excludes))
    with urllib.request.urlopen(url) as response:
        with tarfile.open(fileobj=response, mode="r|xz") as archive:
            for member in archive:
                if not member.name.startswith(prefix):
                    continue
                name = member.name[len(prefix):]
                path = PurePosixPath(name)
                if path.is_absolute() or ".." in path.parts:
                    raise ValueError(f"unsafe archive path: {name}")
                selected = name.startswith(prefixes) or include_regex.fullmatch(name)
                required = name == "third_party/boringssl/src/crypto/fuzzer_mode.cc"
                if not selected or (exclude_regex.fullmatch(name) and not required):
                    continue
                member = copy.copy(member)
                member.name = name
                if member.islnk() and member.linkname.startswith(prefix):
                    member.linkname = member.linkname[len(prefix):]
                archive.extract(member, args.output, filter="data")
                count += 1
    metadata = {
        "version": args.version,
        "url": url,
        "members": count,
        "include_sha256": hashlib.sha256((root / "tools/include.txt").read_bytes()).hexdigest(),
        "exclude_sha256": hashlib.sha256((root / "tools/exclude.txt").read_bytes()).hexdigest(),
    }
    (args.output / "upstream-stage.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata), flush=True)


if __name__ == "__main__":
    main()
