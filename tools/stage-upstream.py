#!/usr/bin/env python3
"""Stage a Chromium release without replacing sources or rewriting Git history.

Usage: python3 tools/stage-upstream.py VERSION OUTPUT_DIRECTORY
The output directory must not exist. Uses tools/include.txt and exclude.txt;
downloads are streamed, so the full Chromium archive is not stored locally.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", args.version):
        parser.error("version must contain four numeric components")
    root = Path(__file__).resolve().parent.parent
    include_file = root / "tools/include.txt"
    exclude_file = root / "tools/exclude.txt"
    includes = include_file.read_text().splitlines()
    args.output.mkdir(parents=True, exist_ok=False)
    prefix = f"chromium-{args.version}/"
    url = f"https://commondatastorage.googleapis.com/chromium-browser-official/chromium-{args.version}.tar.xz"
    # GNU tar handles selection and decompression, as in the upstream importer.
    # curl's progress meter goes to stderr; both process failures are checked.
    patterns = [prefix + pattern.removeprefix("^") for pattern in includes]
    print(f"Staging {url} into {args.output.resolve()}", flush=True)
    with subprocess.Popen(
        ["curl", "--fail", "--location", "--show-error", url],
        stdout=subprocess.PIPE,
    ) as download:
        try:
            extraction = subprocess.run(
                ["tar", "--extract", "--xz", "--file=-",
                 "--directory", str(args.output.resolve()),
                 "--strip-components=1", "--wildcards", "--wildcards-match-slash",
                 "--exclude-from", str(exclude_file), "--", *patterns],
                stdin=download.stdout,
            )
            download.stdout.close()
            download_status = download.wait()
        except BaseException:
            download.terminate()
            raise
    if extraction.returncode or download_status:
        raise RuntimeError(
            f"Staging failed: curl={download_status}, tar={extraction.returncode}; "
            "the incomplete directory has no completion marker"
        )
    metadata = {
        "version": args.version,
        "url": url,
        "extractor": "curl | GNU tar --xz",
        "include_sha256": hashlib.sha256(include_file.read_bytes()).hexdigest(),
        "exclude_sha256": hashlib.sha256(exclude_file.read_bytes()).hexdigest(),
    }
    (args.output / "upstream-stage.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata), flush=True)


if __name__ == "__main__":
    main()
