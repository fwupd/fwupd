#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Install fwupd onto a running Android emulator and verify the fwupd daemon and
# fwupdmgr talk to each other over binder.
#
# Run locally against a device with:
#   DESTDIR="$PWD/dist-android" meson install -C build
#   ./contrib/ci/ubuntu-android-emulator-test.py --source-root . --builddir ./build --destdir ./dist-android

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

# hard-coded as localstatedir for android in the top-level meson.build
LOCALSTATEDIR = "/data/vendor/fwupd"
SERVICE = "org.freedesktop.fwupd.IFwupd/default"
DAEMON_LOG = "/data/local/tmp/fwupd-daemon.log"
GCOV_DEVICE_DIR = "/data/local/tmp/fwupd-gcov"


def adb(
    *args: str,
    check: bool = True,
    capture: bool = False,
    quiet: bool = False,
    timeout: float | None = None,
):
    return subprocess.run(
        ["adb", *args],
        check=check,
        timeout=timeout,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.DEVNULL if quiet else None,
    )


def adb_shell(cmd: str, check: bool = True, quiet: bool = False) -> int:
    return adb("shell", cmd, check=check, quiet=quiet).returncode


def adb_shell_output(cmd: str) -> str:
    try:
        rv = adb("shell", cmd, check=False, capture=True, quiet=True)
    except OSError:
        return ""
    return rv.stdout.replace("\r", "")


def get_prefix(builddir: str) -> str:
    # meson may not be installed on the emulator host, so fall back to reading
    # the introspection data directly
    if shutil.which("meson"):
        data = subprocess.run(
            ["meson", "introspect", "--buildoptions", builddir],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout
    else:
        with open(
            os.path.join(builddir, "meson-info", "intro-buildoptions.json"),
            encoding="utf-8",
        ) as f:
            data = f.read()
    for opt in json.loads(data):
        if opt["name"] == "prefix":
            return opt["value"]
    raise ValueError("no prefix in meson build options")


def wait_boot() -> None:
    """wait until the device is both connected and finished booting"""
    try:
        adb("wait-for-device", timeout=300)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        raise RuntimeError("device did not reconnect to adb") from e
    for _ in range(60):
        if adb_shell_output("getprop sys.boot_completed").strip() == "1":
            return
        time.sleep(5)
    raise RuntimeError("device did not finish booting")


def wait_service_registered() -> bool:
    for _ in range(30):
        if "org.freedesktop.fwupd.IFwupd" in adb_shell_output("service list"):
            return True
        time.sleep(2)
    return False


def cleanup(daemon: str) -> None:
    print("=== fwupd daemon log ===", flush=True)
    adb_shell(f"cat {DAEMON_LOG} 2>/dev/null", check=False)
    print("=== logcat (fwupd) ===", flush=True)
    for line in adb_shell_output("logcat -d").splitlines():
        if "fwupd" in line.lower():
            print(line)
    adb_shell(f"pkill -f {daemon}", check=False, quiet=True)


def run_tests(args: argparse.Namespace, prefix: str) -> int:
    daemon = f"{prefix}/libexec/fwupd/fwupd"
    fwupdmgr = f"{prefix}/bin/fwupdmgr"
    coverage_dest = os.environ.get(
        "COVERAGE_DEST", os.path.join(os.getcwd(), "emulator-gcov")
    )

    wait_boot()
    # AOSP userdebug images allow rooting; needed to write /data and relax SELinux
    adb("root")
    wait_boot()
    adb_shell("setenforce 0", check=False)

    try:
        # push the install tree to where the binaries expect to find themselves
        adb_shell(f"rm -rf {prefix}/")
        subprocess.run(
            [
                os.path.join(
                    args.source_root, "contrib", "android", "adb-push-sync.sh"
                ),
                f"{args.destdir}{prefix}",
                prefix,
            ],
            check=True,
            env={**os.environ, "ADB_FLAGS": "-e"},
        )
        adb_shell(f"mkdir -p {LOCALSTATEDIR}/run {prefix}/cache {GCOV_DEVICE_DIR}")

        # shared library search path: the versioned private libdir plus the main libdir
        libdir_pkg = adb_shell_output(f"echo {prefix}/lib/fwupd-*").strip()
        env_common = " ".join(
            [
                "FWUPD_POLKIT_NOCHECK=1",
                f"CACHE_DIRECTORY={prefix}/cache",
                f"FWUPD_LOCKDIR={LOCALSTATEDIR}/run",
                f"LD_LIBRARY_PATH={libdir_pkg}:{prefix}/lib",
                f"GCOV_PREFIX={GCOV_DEVICE_DIR}",
            ]
        )

        # start the daemon detached; it keeps running after this adb shell returns
        print("starting fwupd daemon...", flush=True)
        adb_shell(
            f"G_MESSAGES_DEBUG=all {env_common} nohup {daemon} "
            f">{DAEMON_LOG} 2>&1 </dev/null &"
        )

        # wait for the daemon to register its binder service
        print(f"waiting for {SERVICE} to register...", flush=True)
        if not wait_service_registered():
            print(f"error: fwupd daemon did not register {SERVICE}", file=sys.stderr)
            return 1
        print("daemon registered", flush=True)

        # diagnostics: show what actually registered with servicemanager
        print("=== binder services matching fwupd ===", flush=True)
        matches = [
            line
            for line in adb_shell_output("service list").splitlines()
            if "fwupd" in line.lower()
        ]
        print("\n".join(matches) if matches else "(none listed)", flush=True)

        # exercise the client -> daemon binder path; exit code 2 is "nothing to do"
        rc = 0
        for cmd in ["get-plugins", "get-devices"]:
            print(f"=== fwupdmgr {cmd} ===", flush=True)
            cmd_rc = adb_shell(f"{env_common} {fwupdmgr} {cmd}", check=False)
            print(f"fwupdmgr {cmd} exit={cmd_rc}", flush=True)
            if cmd_rc not in [0, 2]:
                rc = cmd_rc

        # stop the daemon so it flushes the coverage data
        print("collecting coverage...", flush=True)
        if adb_shell(f"{env_common} {fwupdmgr} quit", check=False, quiet=True) != 0:
            adb_shell(f"pkill -TERM -f {daemon}", check=False, quiet=True)
        time.sleep(5)
        shutil.rmtree(coverage_dest, ignore_errors=True)
        os.makedirs(coverage_dest)
        rv = adb("pull", GCOV_DEVICE_DIR, coverage_dest, check=False, quiet=True)
        if rv.returncode != 0:
            print("(no coverage data pulled)", flush=True)

        if rc == 0:
            print("android emulator smoke test passed", flush=True)
        return rc
    finally:
        cleanup(daemon)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the fwupd daemon and fwupdmgr on an Android emulator"
    )
    parser.add_argument(
        "--source-root", default=".", help="the fwupd source checkout (default: .)"
    )
    parser.add_argument(
        "--builddir",
        default="./build",
        help="the meson build directory (default: ./build)",
    )
    parser.add_argument(
        "--destdir",
        default="./dist-android",
        help="the install tree staged by 'meson install' (default: ./dist-android)",
    )
    args = parser.parse_args()
    if not args.source_root:
        parser.error("--source-root cannot be empty")

    # the binaries' rpath and the baked-in paths assume the tree lives here on-device
    prefix = get_prefix(args.builddir)
    if not os.path.isdir(f"{args.destdir}{prefix}"):
        print(
            f"error: {args.destdir}{prefix} not found -- "
            "run 'meson install --destdir' first",
            file=sys.stderr,
        )
        return 1

    try:
        return run_tests(args, prefix)
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
