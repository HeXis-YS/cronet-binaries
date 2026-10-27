#!/usr/bin/env python3
"""Apply the ordered native Chromium patches to a staged source directory.

Usage: python3 tools/apply-upstream-patches.py SOURCE_DIRECTORY
Stops on the first conflicting patch. The stage directory is disposable; the
repository source tree is never changed by this command.
"""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    if not (source / "chrome/VERSION").is_file():
        parser.error("source must be a staged Chromium tree")
    patches = Path(__file__).resolve().parent / "patches"
    for name in (patches / "series").read_text().splitlines():
        print(f"Applying {name}", flush=True)
        subprocess.run(
            ["git", "apply", "--unsafe-paths", "-p2", f"--directory={source}", str(patches / name)],
            check=True,
        )


if __name__ == "__main__":
    main()
