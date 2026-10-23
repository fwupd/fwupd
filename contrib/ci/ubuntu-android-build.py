#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Cross-build fwupd for Android, run the tests under the host-side Bionic
# runtime, and stage the install and coverage trees for the emulator job.

import os
import shlex
import shutil
import subprocess
import tarfile
from collections.abc import Iterator

ANDROID_SDK_ROOT = "/opt/android/sdk"
NDK_PREBUILT = "/opt/android/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64"
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".cc", ".cxx", ".hpp")
HEADER_SUFFIXES = (".h", ".hpp")


def run(*args: str, env: dict[str, str] | None = None) -> None:
    print(f"+ {shlex.join(args)}", flush=True)
    subprocess.run(args, check=True, env=env)


def walk_files(top: str, prune: str | None = None) -> Iterator[str]:
    for dirpath, dirnames, filenames in os.walk(top):
        if prune and dirpath == top:
            dirnames[:] = [d for d in dirnames if os.path.join(top, d) != prune]
        for fn in filenames:
            yield os.path.join(dirpath, fn)


def coverage_tree_files() -> Iterator[str]:
    """what the emulator job needs to find the install prefix and to turn the
    fwupd .gcda into a coverage report"""
    for fn in walk_files("build", prune="build/subprojects"):
        if fn.endswith(".gcno") or fn.endswith(SOURCE_SUFFIXES):
            yield fn
    yield from walk_files("build/meson-info")
    for fn in walk_files("subprojects"):
        if fn.endswith(HEADER_SUFFIXES):
            yield fn


def make_tarball(filename: str, files: Iterator[str]) -> None:
    print(f"creating {filename}", flush=True)
    with tarfile.open(filename, "w:gz") as tar:
        for fn in sorted(set(files)):
            tar.add(fn, recursive=False)


def main() -> None:
    root = os.getcwd()
    build = os.path.join(root, "build")
    dist = os.path.join(root, "dist-android")

    # check for NDK, SDK and Bionic runtime
    run(os.path.join(root, "contrib", "ci", "android.py"))

    # make aidl and exe_wrapper available
    env = dict(os.environ)
    env["ANDROID_SDK_ROOT"] = ANDROID_SDK_ROOT
    env["PATH"] = os.pathsep.join(
        [
            os.path.join(root, "contrib", "android"),
            env.get("PATH", ""),
            f"{ANDROID_SDK_ROOT}/cmdline-tools/bin",
            f"{ANDROID_SDK_ROOT}/build-tools/35.0.0",
        ]
    )
    env["BUILD"] = build

    shutil.rmtree(build, ignore_errors=True)
    run(
        "meson",
        "setup",
        build,
        "-Db_coverage=true",
        "--cross-file",
        "contrib/android/android_x86_64-cross-file.ini",
        "--cross-file",
        "contrib/ci/android-ci.ini",
        "--prefix=/data/fwupd",
        env=env,
    )
    run("ninja", "-C", build, "-v", env=env)
    run("ninja", "-C", build, "test", env=env)

    # smoke-test the cross-built fwupdtool runs under the host-side Bionic wrapper
    env["FWUPD_LOCKDIR"] = os.path.join(build, "lockdir")
    os.makedirs(env["FWUPD_LOCKDIR"], exist_ok=True)
    fwupdtool = os.path.join(build, "src", "fwupdtool")
    run("android-exe-wrapper.sh", fwupdtool, "--version", env=env)
    run("android-exe-wrapper.sh", fwupdtool, "get-plugins", env=env)

    # stage an install tree for the emulator job
    shutil.rmtree(dist, ignore_errors=True)
    run("meson", "install", "-C", build, env={**env, "DESTDIR": dist})
    with tarfile.open(os.path.join(root, "dist-android.tar.gz"), "w:gz") as tar:
        tar.add(dist, arcname=".")
    make_tarball(
        os.path.join(root, "coverage-tree-android.tar.gz"), coverage_tree_files()
    )

    # generate coverage report using llvm-cov from the NDK
    env["GCOV"] = f"{NDK_PREBUILT}/bin/llvm-cov gcov"
    run(os.path.join(root, "contrib", "ci", "coverage.py"), env=env)


if __name__ == "__main__":
    main()
