#!/usr/bin/env python3
"""Install a completed, patched stage without rewriting repository history.

Run stage-upstream.py and apply-upstream-patches.py first. Local build scripts,
the Xray native adapter, and untracked toolchain/build caches are preserved.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    stage = args.source.resolve()
    metadata = json.loads((stage / "upstream-stage.json").read_text())
    root = Path(__file__).resolve().parent.parent
    destination = root / "src"
    files = {
        path.relative_to(stage): path
        for path in stage.rglob("*")
        if path.is_file() or path.is_symlink()
    }
    files.pop(Path("upstream-stage.json"))

    def local(path):
        return path == Path("BUILD.gn") or path.parts[:2] == ("components", "xray_native") or (
            len(path.parts) == 1 and path.suffix == ".sh"
        )

    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "src"], cwd=root
    ).decode().split("\0")
    for name in filter(None, tracked):
        relative = Path(name).relative_to("src")
        if not local(relative) and relative not in files:
            (root / name).unlink(missing_ok=True)
    for relative, source in files.items():
        if local(relative):
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_symlink():
            target.unlink()
        shutil.copy2(source, target, follow_symlinks=False)
    (root / "CHROMIUM_VERSION").write_text(metadata["version"] + "\n")
    print(f"Installed Chromium {metadata['version']}: {len(files)} files")


if __name__ == "__main__":
    main()
