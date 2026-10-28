"""Installer behavior at filesystem and executable boundaries; no downloads.

Run: python3 -m unittest discover -s tools/tests -p 'test_install_android_tools.py' -v
"""

import importlib.util
import os
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "android_tools", ROOT / "src/install-android-tools.py")
TOOLS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOLS)

NDK_NAME = "chromium/third_party/android_toolchain/android_toolchain/linux-amd64"
NDK_VERSION = "version:2@30.0.16248370"
NDK_DIRECTORY = "third_party/android_toolchain/ndk"
JDK_NAME = "chromium/third_party/jdk/linux-amd64"
JDK_VERSION = "version:2@jdk-99.0.1+2.d123456.cr0"
JDK_DIRECTORY = "third_party/jdk/current"
SDK_DIRECTORY = "third_party/android_sdk/public"
PLATFORM_NAME = "chromium/third_party/android_sdk/public/platforms/android-37.0"
PLATFORM_PIN = "fixed-platform-instance-id"


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def executable(path, output):
    write(path, "#!/bin/sh\nprintf '%s\\n' " + shlex.quote(output) + "\n")
    path.chmod(0o755)


def ndk(directory, build="16248370"):
    header = directory / TOOLS.NDK_SYSROOT / "usr/include/android/ndk-version.h"
    write(header, "#define __NDK_MAJOR__ 30\n#define __NDK_MINOR__ 0\n"
          f"#define __NDK_BUILD__ {build}\n#define __NDK_BETA__ 0\n")
    write(header.with_name("api-level.h"), "/* Native API declarations */\n")
    for arch in ("aarch64-linux-android", "arm-linux-androideabi",
                 "i686-linux-android", "x86_64-linux-android"):
        write(directory / TOOLS.NDK_SYSROOT / f"usr/lib/{arch}/24/libc.so",
              "native system library")


def jdk(directory):
    write(directory / "release", 'JAVA_RUNTIME_VERSION="99.0.1+2-LTS"\n')
    for name in ("include/jni.h", "include/linux/jni_md.h", "lib/modules"):
        write(directory / name, "test JDK data")
    for name in ("java", "javac", "javap"):
        executable(directory / "bin" / name, "OpenJDK 99.0.1+2-LTS")


def sdk_platform(directory):
    write(directory / "source.properties", "AndroidVersion.ApiLevel=37.0\nPkg.Revision=2\n")
    with zipfile.ZipFile(directory / "android.jar", "w") as jar:
        jar.writestr("java/lang/Object.class", b"\xca\xfe\xba\xbe")


class InstallerChecks(unittest.TestCase):
    def setUp(self):
        runs = ROOT / ".codex/runs"
        runs.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="android-tool-tests-", dir=runs)
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.root = self.directory / "checkout"
        # Isolate runner discovery from the host environment. The real unzip
        # remains available; curl is replaced only at the download boundary.
        environment = {key: "" for key in (
            "ANDROID_HOME", "ANDROID_SDK_ROOT", "ANDROID_NDK_LATEST_HOME",
            "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK", "JAVA_HOME",
            "JAVA_HOME_99_X64")}
        environment["PATH"] = str(self.directory / "bin") + os.pathsep + os.environ["PATH"]
        self.environment_patch = patch.dict(os.environ, environment)
        self.environment_patch.start()
        self.addCleanup(self.environment_patch.stop)
        executable(self.directory / "bin/curl", "Unexpected download")
        write(self.directory / "bin/curl", "#!/bin/sh\nexit 99\n")

    def stamp(self, directory, name, version):
        path = directory / ("." + name.replace("/", "_") + ".version")
        write(path, version)
        return path

    def download_archive(self, source):
        archive = self.directory / "package.zip"
        with zipfile.ZipFile(archive, "w") as package:
            for path in source.rglob("*"):
                if path.is_file():
                    package.write(path, path.relative_to(source))
        os.environ["ANDROID_TEST_ARCHIVE"] = str(archive)
        write(self.directory / "bin/curl", "#!/usr/bin/env python3\n"
              "import os, shutil, sys\n"
              "shutil.copyfile(os.environ['ANDROID_TEST_ARCHIVE'], "
              "sys.argv[sys.argv.index('-o') + 1])\n")

    def test_existing_tools_without_stamp_do_not_download(self):
        target = self.root / NDK_DIRECTORY
        ndk(target)
        self.assertTrue(TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME, NDK_VERSION))
        self.assertEqual((target / ("." + NDK_NAME.replace("/", "_") + ".version")).read_text(),
                         NDK_VERSION)

    def test_matching_stamp_does_not_hide_missing_library(self):
        target = self.root / NDK_DIRECTORY
        ndk(target)
        stamp = self.stamp(target, NDK_NAME, NDK_VERSION)
        (target / TOOLS.NDK_SYSROOT / "usr/lib/aarch64-linux-android/24/libc.so").unlink()
        self.assertFalse(TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME,
                                             NDK_VERSION, check_only=True))
        self.assertEqual(stamp.read_text(), NDK_VERSION)  # --check writes nothing.

    def test_matching_runner_ndk_is_copied_and_rechecked(self):
        source = self.directory / "runner/ndk"
        ndk(source)
        os.environ["ANDROID_NDK_LATEST_HOME"] = str(source)
        self.assertTrue(TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME, NDK_VERSION))
        target = self.root / NDK_DIRECTORY
        self.assertIsNone(TOOLS.validate(target, NDK_NAME, NDK_VERSION))
        self.assertFalse(target.is_symlink())

    def test_wrong_runner_ndk_version_is_rejected(self):
        source = self.directory / "runner/ndk"
        ndk(source, build="1")
        os.environ["ANDROID_NDK_LATEST_HOME"] = str(source)
        self.assertFalse(TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME,
                                             NDK_VERSION, check_only=True))
        self.assertFalse(self.root.exists())

    def test_jdk_requires_usable_executables_with_matching_output(self):
        target = self.root / JDK_DIRECTORY
        jdk(target)
        self.assertIsNone(TOOLS.validate(target, JDK_NAME, JDK_VERSION))
        (target / "bin/javap").chmod(0o644)
        self.assertIn("not executable", TOOLS.validate(target, JDK_NAME, JDK_VERSION))
        executable(target / "bin/javap", "OpenJDK 98.0.1")
        self.assertIn("unexpected output", TOOLS.validate(target, JDK_NAME, JDK_VERSION))
        write(target / "bin/java", "#!/bin/sh\nexit 7\n")
        self.assertIsNotNone(TOOLS.validate(target, JDK_NAME, JDK_VERSION))

    def test_build_tools_check_revision_and_execution(self):
        name = "chromium/third_party/android_sdk/public/build-tools/37.0.0/linux"
        target = self.root / SDK_DIRECTORY / "build-tools/37.0.0"
        write(target / "source.properties", "Pkg.Revision=37.0.0\n")
        for tool in ("aidl", "zipalign", "aapt2"):
            executable(target / tool, "Android Asset Packaging Tool (aapt) 2")
        self.assertIsNone(TOOLS.validate(target, name, "version_37.0.0"))
        write(target / "source.properties", "Pkg.Revision=36.0.0\n")
        self.assertIn("expected 37.0.0", TOOLS.validate(target, name, "version_37.0.0"))

    def test_platform_api_alone_does_not_override_pinned_revision(self):
        sdk = self.root / SDK_DIRECTORY
        target = sdk / "platforms/android-37.0"
        sdk_platform(target)
        self.assertFalse(TOOLS.ensure_package(self.root, SDK_DIRECTORY, PLATFORM_NAME,
                                             PLATFORM_PIN, check_only=True))
        self.stamp(sdk, PLATFORM_NAME, PLATFORM_PIN)
        self.assertTrue(TOOLS.ensure_package(self.root, SDK_DIRECTORY, PLATFORM_NAME,
                                            PLATFORM_PIN, check_only=True))
        write(target / "android.jar", "corrupt archive")
        self.assertFalse(TOOLS.ensure_package(self.root, SDK_DIRECTORY, PLATFORM_NAME,
                                             PLATFORM_PIN, check_only=True))

    def test_download_is_validated_before_stamp_is_written(self):
        payload = self.directory / "payload"
        ndk(payload)
        self.download_archive(payload)
        self.assertTrue(TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME, NDK_VERSION))
        # A later run must validate the files and avoid downloading again.
        write(self.directory / "bin/curl", "#!/bin/sh\nexit 99\n")
        self.assertTrue(TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME, NDK_VERSION))

    def test_incomplete_installation_removes_old_stamp_and_fails(self):
        payload = self.directory / "payload"
        ndk(payload)
        (payload / TOOLS.NDK_SYSROOT / "usr/include/android/api-level.h").unlink()
        self.download_archive(payload)
        stamp = self.stamp(self.root / NDK_DIRECTORY, NDK_NAME, NDK_VERSION)
        with self.assertRaisesRegex(RuntimeError, "Installed tool validation failed"):
            TOOLS.ensure_package(self.root, NDK_DIRECTORY, NDK_NAME, NDK_VERSION)
        self.assertFalse(stamp.exists())


if __name__ == "__main__":
    unittest.main()
