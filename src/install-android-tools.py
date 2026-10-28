#!/usr/bin/env python3
"""Install native Android and JNI generation tools pinned by Chromium DEPS.

Run from a prepared source tree on Linux x64. Requires curl and unzip.
--list prints the selected packages without downloading them.
--check checks the checkout and CI environment without installing anything.
"""

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import urllib.parse
import zipfile


NDK_SYSROOT = Path("toolchains/llvm/prebuilt/linux-x86_64/sysroot")


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


def component(name):
    """Return the component directory within a CIPD package's destination."""
    if "/build-tools/" in name:
        return Path("build-tools") / name.split("/build-tools/")[1].split("/")[0]
    if "/platforms/" in name:
        return Path("platforms") / name.rsplit("/", 1)[1]
    return Path(".")


def properties(path):
    return dict(line.strip().split("=", 1) for line in path.read_text().splitlines()
                if "=" in line and not line.lstrip().startswith("#"))


def validate(directory, name, version):
    """Return a diagnostic for an unusable component, or None if it passes."""
    try:
        if "/android_toolchain/" in name:
            header = (directory / NDK_SYSROOT / "usr/include/android/ndk-version.h").read_text()
            values = dict(re.findall(r"^#define __NDK_(\w+)__\s+(\d+)", header, re.M))
            actual = ".".join(values[key] for key in ("MAJOR", "MINOR", "BUILD"))
            expected = version.split("@", 1)[1]
            if actual != expected or values["BETA"] != "0":
                return f"NDK {actual} beta={values['BETA']}; expected {expected} release"
            required = [NDK_SYSROOT / "usr/include/android/api-level.h"]
            for arch in ("aarch64-linux-android", "arm-linux-androideabi",
                         "i686-linux-android", "x86_64-linux-android"):
                required.append(NDK_SYSROOT / f"usr/lib/{arch}/24/libc.so")
            commands = []  # Chromium supplies Clang separately from this NDK.
        elif "/jdk/" in name:
            release = properties(directory / "release")
            # The .d...crN suffix is Chromium's packaging revision, not Java's
            # runtime version. Compare the full upstream Java build number.
            expected = version.split("@jdk-", 1)[1].split(".d", 1)[0]
            actual = release["JAVA_RUNTIME_VERSION"].strip('"').removesuffix("-LTS")
            if actual != expected:
                return f"JDK {actual}; expected {expected}"
            required = [Path("include/jni.h"), Path("include/linux/jni_md.h"),
                        Path("lib/modules")]
            commands = [("bin/java", "-version", expected),
                        ("bin/javap", "-version", expected.split("+", 1)[0]),
                        ("bin/javac", "-version", expected.split("+", 1)[0])]
        else:
            metadata = properties(directory / "source.properties")
            if "/build-tools/" in name:
                expected = component(name).name
                if metadata["Pkg.Revision"] != expected:
                    return f"build-tools {metadata['Pkg.Revision']}; expected {expected}"
                required = [Path("aidl"), Path("zipalign")]
                commands = [("aapt2", "version", "Android Asset Packaging Tool")]
                for executable in required:
                    if not os.access(directory / executable, os.X_OK):
                        return f"not executable: {directory / executable}"
            else:
                expected = component(name).name.removeprefix("android-")
                if metadata["AndroidVersion.ApiLevel"] != expected:
                    return f"SDK API {metadata['AndroidVersion.ApiLevel']}; expected {expected}"
                required = [Path("android.jar")]
                commands = []
                with zipfile.ZipFile(directory / "android.jar") as jar:
                    jar.read("java/lang/Object.class")
        for path in required:
            full_path = directory / path
            if not full_path.is_file() or full_path.stat().st_size == 0:
                return f"missing or empty file: {full_path}"
        for executable, argument, expected_output in commands:
            path = directory / executable
            if not path.is_file() or not os.access(path, os.X_OK):
                return f"not executable: {path}"
            result = subprocess.run([str(path), argument], check=True, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if expected_output not in result.stdout:
                return f"unexpected output from {path}: {result.stdout.strip()}"
    except (OSError, KeyError, ValueError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        return str(error)
    return None


def candidates(name, version):
    """Discover runner tools through the environment, without choosing versions."""
    sdk_roots = [Path(os.environ[key]) for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT")
                 if os.environ.get(key)]
    if "/android_toolchain/" in name:
        paths = [Path(os.environ[key]) for key in (
            "ANDROID_NDK_LATEST_HOME", "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK"
        ) if os.environ.get(key)]
        paths += [sdk / "ndk" / version.split("@", 1)[1] for sdk in sdk_roots]
    elif "/jdk/" in name:
        major = version.split("@jdk-", 1)[1].split(".", 1)[0]
        paths = [Path(os.environ[key]) for key in ("JAVA_HOME", f"JAVA_HOME_{major}_X64")
                 if os.environ.get(key)]
        java = shutil.which("java")
        if java:
            paths.append(Path(java).resolve().parents[1])
    else:
        paths = [sdk / component(name) for sdk in sdk_roots]
    return list(dict.fromkeys(path.resolve() for path in paths))


def ensure_package(root, directory, name, version, check_only=False):
    destination = root / directory
    target = destination / component(name)
    stamp_name = "." + name.replace("/", "_") + ".version"
    stamp = destination / stamp_name
    # Platform pins are opaque CIPD instance IDs: the API level alone cannot
    # prove that a runner's android.jar is the revision required by DEPS.
    needs_pin = "/platforms/" in name
    for source in dict.fromkeys([target.resolve(), *candidates(name, version)]):
        error = validate(source, name, version)
        if error is None and needs_pin:
            source_stamp = source.parent.parent / stamp_name
            if not source_stamp.is_file() or source_stamp.read_text() != version:
                error = "SDK platform CIPD revision cannot be confirmed"
        if error is not None:
            print(f"Cannot reuse {source}: {error}", flush=True)
            continue
        print(f"Validated {name} {version}: {source}", flush=True)
        if not check_only and source != target.resolve():
            shutil.copytree(source, target, symlinks=True, dirs_exist_ok=True)
            error = validate(target, name, version)
            if error:
                raise RuntimeError(f"Copied tool validation failed: {error}")
        if not check_only:
            destination.mkdir(parents=True, exist_ok=True)
            stamp.write_text(version)
        return True
    if check_only:
        return False
    destination.mkdir(parents=True, exist_ok=True)
    stamp.unlink(missing_ok=True)
    url = "https://chrome-infra-packages.appspot.com/dl/" + name
    url += "/+/" + urllib.parse.quote(version, safe="")
    archive = destination / ".install-package.zip"
    print(f"Installing {name} {version} into {directory}", flush=True)
    subprocess.run(["curl", "--fail", "--location", "--show-error",
                    url, "-o", str(archive)], check=True)
    # unzip preserves the executable permissions recorded in CIPD archives.
    subprocess.run(["unzip", "-oq", str(archive), "-d", str(destination)], check=True)
    archive.unlink()
    error = validate(target, name, version)
    if error:
        raise RuntimeError(f"Installed tool validation failed for {name}: {error}")
    stamp.write_text(version)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--list", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    selected = list(packages(root))
    if args.list:
        print(json.dumps(selected, indent=2))
        return
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("Android tool installation requires a Linux x64 host")
    available = [ensure_package(root, directory, name, version, args.check)
                 for directory, name, version in selected]
    if not all(available):
        parser.exit(1, "Required Android tools are missing or do not match DEPS\n")


if __name__ == "__main__":
    main()
