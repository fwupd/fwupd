# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'test' subcommand — run the fwupd test suite

import argparse
import os
import shutil
import signal
import subprocess
import sys
import time

from .helpers import (
    banner,
    build_path,
    dist_path,
    error,
    logger,
    msg,
    require_build,
    venv_path,
    RunCmd,
)

VALID_TESTS = ("all", "meson", "mtd", "fwupdtool", "fwupd")
SUDO_MODES = ("auto", "noask", "skip")


def register(subparsers):
    """Register the 'test' subcommand with argparse."""
    parser = subparsers.add_parser(
        "test",
        help="run fwupd test suite",
        description=(
            "Run the fwupd test suite. By default all tests are run. "
            "Use --tests to select a subset."
        ),
    )
    parser.add_argument(
        "--tests",
        default="all",
        help=(
            "comma-separated list of tests to run: "
            "all, meson, mtd, fwupdtool, fwupd (default: all)"
        ),
    )
    parser.add_argument(
        "--sudo",
        dest="sudo_mode",
        choices=SUDO_MODES,
        default="auto",
        help=(
            "behavior for sudo: "
            "auto=ask if needed (default), "
            "noask=only if already authenticated, "
            "skip=skip tests requiring sudo"
        ),
    )
    parser.add_argument(
        "--meson-args",
        dest="meson_args",
        nargs=argparse.REMAINDER,
        default=[],
        help="extra arguments passed to meson test",
    )
    parser.set_defaults(func=run)


def _parse_tests(tests_str):
    """Parse the --tests argument into a set of test names."""
    tests = set()
    for t in tests_str.split(","):
        t = t.strip()
        if t == "all":
            return set(VALID_TESTS) - {"all"}
        if t not in VALID_TESTS:
            logger.error(f"Unknown test name: '{t}'")
            logger.error(f"Valid tests: {', '.join(VALID_TESTS)}")
            sys.exit(1)
        tests.add(t)
    return tests


def _resolve_sudo(sudo_mode):
    """Resolve the sudo binary based on the mode.

    Returns the path to sudo, or empty string if sudo should be skipped.
    """
    match sudo_mode:
        case "skip":
            return ""
        case "noask":
            sudo = shutil.which("sudo")
            if sudo:
                # Check if we can sudo without a password
                if not RunCmd([sudo, "-n", "true"]).success:
                    return ""
            return sudo or ""
        case _:
            return shutil.which("sudo") or ""


def _chown_state_dir(venv, sudo):
    """Fix ownership of the state directory after root tests.

    Tests/tools run as root may create dist/var which can cause
    subsequent tests to fail if they can't write to that directory.
    """
    state_dir = venv / "dist" / "var"
    if sudo and state_dir.is_dir():
        uid = os.getuid()
        gid = os.getgid()
        RunCmd([sudo, "chown", "-R", f"{uid}:{gid}", state_dir])


def run(args):
    """Run the fwupd test suite."""
    require_build(args)

    build = build_path(args)
    dist = dist_path(args)
    venv = venv_path(args)
    installed_tests = dist / "share" / "installed-tests" / "fwupd"

    tests = _parse_tests(args.tests)
    sudo = _resolve_sudo(args.sudo_mode)

    # Set up environment
    env = os.environ.copy()
    env["G_TEST_BUILDDIR"] = str(installed_tests)
    env["G_TEST_SRCDIR"] = str(installed_tests)
    env["GI_TYPELIB_PATH"] = str(build / "libfwupd")
    env["LD_LIBRARY_PATH"] = str(build / "libfwupd")
    env["DAEMON_BUILDDIR"] = str(build / "src")
    env["PATH"] = str(venv / "bin") + ":" + env.get("PATH", "")
    env["PYTHONWARNINGS"] = "ignore::DeprecationWarning:gi.events"

    # Track the daemon process and log file for cleanup
    daemon_proc = None
    daemon_log_fh = None

    def cleanup():
        nonlocal daemon_proc, daemon_log_fh
        if daemon_proc is not None and daemon_proc.poll() is None:
            daemon_proc.send_signal(signal.SIGTERM)
            for _ in range(5):
                if daemon_proc.poll() is not None:
                    break
                time.sleep(1)
            if daemon_proc.poll() is None:
                daemon_proc.kill()
            daemon_proc.wait()
            daemon_proc = None
        if daemon_log_fh is not None:
            daemon_log_fh.close()
            daemon_log_fh = None
        _chown_state_dir(venv, sudo)

    # Run meson tests
    if "meson" in tests:
        banner(args, "green", "Testing meson test suite")
        cmd = RunCmd(
            ["meson", "test", "-C", build] + args.meson_args,
            env=env,
            capture=False,
        )
        if not cmd.success:
            return 1

    # Run mtd-self-test
    if "mtd" in tests:
        if not sudo:
            msg(args, "Skipping mtd-self-test (requires sudo)")
        else:
            banner(args, "blue", "Testing mtd-self-test")
            RunCmd([sudo, "modprobe", "mtdram"], capture=False)
            mtd_self_test = (
                dist / "libexec" / "installed-tests" / "fwupd" / "mtd-self-test"
            )
            if mtd_self_test.exists():
                cmd = RunCmd(
                    [
                        sudo,
                        f"G_TEST_BUILDDIR={installed_tests}",
                        f"LD_LIBRARY_PATH={env['LD_LIBRARY_PATH']}",
                        f"G_TEST_SRCDIR={installed_tests}",
                        mtd_self_test,
                    ],
                    capture=False,
                )
                if not cmd.success:
                    cleanup()
                    return 1
            _chown_state_dir(venv, sudo)

    # Run fwupdtool.sh
    if "fwupdtool" in tests:
        if not sudo:
            msg(args, "Skipping fwupdtool test (requires sudo)")
        else:
            fwupdtool_sh = installed_tests / "fwupdtool.sh"
            if fwupdtool_sh.exists():
                banner(args, "yellow", "Testing fwupdtool.sh")
                cmd = RunCmd([fwupdtool_sh], env=env, capture=False)
                if not cmd.success:
                    cleanup()
                    return 1
                # Clean up artifacts
                fwupdtool_txt = venv.parent / "fwupdtool.txt"
                if fwupdtool_txt.exists():
                    fwupdtool_txt.unlink()
                _chown_state_dir(venv, sudo)

    # Run fwupd.sh (start daemon, run integration tests)
    if "fwupd" in tests:
        if not sudo:
            msg(args, "Skipping fwupd test (requires sudo)")
        else:
            fwupd_sh = installed_tests / "fwupd.sh"
            if fwupd_sh.exists():
                fwupd_bin = venv / "bin" / "fwupd"
                banner(args, "pink", "Starting daemon")

                daemon_env = env.copy()
                daemon_env["G_DEBUG"] = "fatal-criticals"
                fwupd_log = venv.parent / "fwupd.txt"
                # stdin needs to be DEVNULL because *something*
                # changes onlcr causing a staircase pattern in
                # stdout (if stdout is a tty).
                daemon_log_fh = fwupd_log.open("w")
                daemon_proc = subprocess.Popen(
                    [fwupd_bin, "--verbose", "--no-timestamp"],
                    stdin=subprocess.DEVNULL,
                    stdout=daemon_log_fh,
                    stderr=subprocess.STDOUT,
                    env=daemon_env,
                )

                try:
                    # Give the daemon time to create state directories
                    time.sleep(0.5)
                    _chown_state_dir(venv, sudo)

                    banner(args, "pink", "Testing fwupd.sh")
                    cmd = RunCmd(
                        [fwupd_sh],
                        env=env,
                        capture=False,
                        stdin=subprocess.DEVNULL,
                    )
                    if not cmd.success:
                        return 1
                finally:
                    cleanup()

                # Clean up artifacts
                if fwupd_log.exists():
                    fwupd_log.unlink()

    return 0
