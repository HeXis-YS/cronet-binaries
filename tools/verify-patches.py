#!/usr/bin/env python3
"""Verify the series against pinned upstream files, without a Chromium build.

Usage: python3 tools/verify-patches.py NEW_OUTPUT_DIRECTORY
Requires network access and Git. Saves upstream bytes, source hashes, applied
files, repository conditions and command output in the output directory.
This verifies patch applicability, not compilation or runtime behavior.
"""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-cache", type=Path,
                        help="Reuse upstream files from a previous run of this verifier")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    version = (root / "CHROMIUM_VERSION").read_text().strip()
    series = (root / "tools/patches/series").read_text().splitlines()
    inputs = set()
    hashes = {}
    for name in series:
        patch = root / "tools/patches" / name
        hashes[name] = hashlib.sha256(patch.read_bytes()).hexdigest()
        for line in patch.read_text().splitlines():
            if line.startswith("--- a/src/"):
                inputs.add(line.removeprefix("--- a/src/"))
    conditions = {
        "chromium": version,
        "revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "patches": hashes,
    }
    (output / "conditions.json").write_text(json.dumps(conditions, indent=2))
    (output / "workspace.diff").write_bytes(subprocess.check_output(
        ["git", "diff", "HEAD"], cwd=root))
    sources = []
    with (output / "verify.log").open("w") as log:
        for path in sorted(inputs):
            url = f"https://chromium.googlesource.com/chromium/src/+/refs/tags/{version}/{path}?format=TEXT"
            cached = args.source_cache / path if args.source_cache else None
            if cached and cached.is_file():
                data = cached.read_bytes()
            else:
                data = base64.b64decode(urllib.request.urlopen(url, timeout=60).read())
            source = output / "upstream" / path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(data)
            sources.append({"path": path, "url": url,
                            "sha256": hashlib.sha256(data).hexdigest()})
            (output / "sources.json").write_text(json.dumps(sources, indent=2))
            print(f"Fetched {path}", flush=True)
        (output / "sources.json").write_text(json.dumps(sources, indent=2))
        shutil.copytree(output / "upstream", output / "applied")
        for name in series:
            command = ["git", "apply", "--verbose", "--whitespace=error", "--unsafe-paths", "-p2",
                       f"--directory={output / 'applied'}",
                       str(root / "tools/patches" / name)]
            print(" ".join(command), file=log, flush=True)
            subprocess.run(command, cwd=root, stdout=log, stderr=log, check=True)
        subprocess.run(["git", "diff", "--check"], cwd=root,
                       stdout=log, stderr=log, check=True)
        for path in sorted((root / "src").glob("*.sh")):
            subprocess.run(["sh", "-n", str(path)], check=True,
                           stdout=log, stderr=log)
        subprocess.run(["bash", "-n", str(root / "tools/prepare-source.sh")],
                       check=True, stdout=log, stderr=log)
        print("PASS: upstream patch application, whitespace and shell syntax", file=log)
    print(f"PASS: {len(series)} patches applied; evidence: {output}")


if __name__ == "__main__":
    main()
