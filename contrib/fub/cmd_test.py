# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'test' subcommand — run the fwupd test suite

import argparse
import os
import shutil
import subprocess
import sys
import time
from contextlib import contextmanager

from .cli import argparse_func_wrapper
from .directories import directories
from .logger import logger, printer
from .meson import Meson
from .osprofile import RunCmd

VALID_TESTS = {"all", "meson", "mtd", "fwupdtool", "fwupd"}
SUDO_MODES = ("auto", "noask", "skip")


def register(subparsers):
    """Register the 'test' subcommand with argparse."""
    parser = subparsers.add_parser(
        "test",
        help="run fwupd test suite",
        description=(
            "Run the fwupd test suite. Runs all tests by default, "
            "use --tests to select a subset."
        ),
    )
    parser.add_argument(
        "--tests",
        default="all",
        choices=VALID_TESTS,
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
    parser.set_defaults(func=argparse_func_wrapper(run))


def _parse_tests(tests_str):
    """Parse the --tests argument into a set of test names."""
    tests = set()
    for t in tests_str.split(","):
        t = t.strip()
        if t == "all":
            return VALID_TESTS - {"all"}
        if t not in VALID_TESTS:
            logger.error(
                f"Unknown test name: '{t}', pick one of {', '.join(VALID_TESTS)}"
            )
            sys.exit(1)
        tests.add(t)
    return tests


def _resolve_sudo(sudo_mode) -> str:
    """
    Resolve the sudo binary based on the mode.

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


@contextmanager
def chown_state_dir(build_root, sudo):
    """Fix ownership of the state directory after root tests.

    Tests/tools run as root may create dist/var which can cause
    subsequent tests to fail if they can't write to that directory.
    """
    yield

    state_dir = build_root / "dist" / "var"
    if sudo and state_dir.is_dir():
        uid = os.getuid()
        gid = os.getgid()
        RunCmd(["chown", "-R", f"{uid}:{gid}", state_dir], as_sudo=True)


def run(args):
    """Run the fwupd test suite."""
    meson = Meson(builddir=directories.builddir(), meson_args=[])
    if meson.needs_setup:
        printer.error("Project not yet built, run [bold]fub build[/bold] first")
        sys.exit(1)

    build_root = directories.build_root()
    builddir = directories.builddir()
    distdir = directories.distdir()
    installed_tests = distdir / "share" / "installed-tests" / "fwupd"

    tests = _parse_tests(args.tests)
    sudo = _resolve_sudo(args.sudo_mode)

    env = os.environ.copy()
    env["G_TEST_BUILDDIR"] = str(installed_tests)
    env["G_TEST_SRCDIR"] = str(installed_tests)
    env["GI_TYPELIB_PATH"] = str(builddir / "libfwupd")
    env["LD_LIBRARY_PATH"] = str(builddir / "libfwupd")
    env["DAEMON_BUILDDIR"] = str(builddir / "src")
    env["PATH"] = str(build_root / "bin") + ":" + env.get("PATH", "")
    env["PYTHONWARNINGS"] = "ignore::DeprecationWarning:gi.events"

    # Run meson tests
    if "meson" in tests:
        printer.banner("Testing meson test suite")
        cmd = meson.test(test_args=args.meson_args, test_env=env)
        if not cmd.success:
            return 1

    # Run mtd-self-test
    if "mtd" in tests:
        if not sudo:
            printer.message("Skipping mtd-self-test (requires sudo)")
        else:
            printer.banner("Testing mtd-self-test")
            with chown_state_dir(build_root, sudo):
                RunCmd(["modprobe", "mtdram"], capture=False, as_sudo=True)
                mtd_self_test = (
                    distdir / "libexec" / "installed-tests" / "fwupd" / "mtd-self-test"
                )
                if mtd_self_test.exists():
                    selftest_env = {
                        "G_TEST_BUILDDIR": installed_tests,
                        "LD_LIBRARY_PATH": env["LD_LIBRARY_PATH"],
                        "G_TEST_SRCDIR": installed_tests,
                    }
                    cmd = RunCmd(
                        [mtd_self_test],
                        capture=False,
                        as_sudo=True,
                        sudo_env=selftest_env,
                    )
                    if not cmd.success:
                        return 1

    # Run fwupdtool.sh
    if "fwupdtool" in tests:
        if not sudo:
            printer.message("Skipping fwupdtool test (requires sudo)")
        else:
            fwupdtool_sh = installed_tests / "fwupdtool.sh"
            if fwupdtool_sh.exists():
                printer.banner("Testing fwupdtool.sh")
                with chown_state_dir(build_root, sudo):
                    cmd = RunCmd([fwupdtool_sh], env=env, capture=False)
                    if not cmd.success:
                        return 1
                    # Clean up artifacts
                    fwupdtool_txt = build_root.parent / "fwupdtool.txt"
                    if fwupdtool_txt.exists():
                        fwupdtool_txt.unlink()

    # Run fwupd.sh (start daemon, run integration tests)
    if "fwupd" in tests:
        if not sudo:
            printer.message("Skipping fwupd test (requires sudo)")
        else:
            fwupd_sh = installed_tests / "fwupd.sh"
            if fwupd_sh.exists():
                fwupd_bin = build_root / "bin" / "fwupd"
                printer.banner("Starting daemon")

                with chown_state_dir(build_root, sudo):
                    daemon_env = env.copy()
                    daemon_env["G_DEBUG"] = "fatal-criticals"
                    fwupd_log = build_root.parent / "fwupd.txt"
                    # stdin needs to be DEVNULL because *something*
                    # changes onlcr causing a staircase pattern in
                    # stdout (if stdout is a tty).
                    with fwupd_log.open("w") as daemon_log_fh:
                        cmd = RunCmd(
                            [fwupd_bin, "--verbose", "--no-timestamp"],
                            stdin=subprocess.DEVNULL,
                            stdout=daemon_log_fh,
                            stderr=subprocess.STDOUT,
                            env=daemon_env,
                            daemon=True,
                        )

                        with cmd:
                            # Give the daemon time to create state directories
                            time.sleep(0.5)
                            printer.banner("Testing fwupd.sh")
                            cmd = RunCmd(
                                [fwupd_sh],
                                env=env,
                                capture=False,
                                stdin=subprocess.DEVNULL,
                            )
                            if not cmd.success:
                                return 1

                            # Clean up artifacts
                            if fwupd_log.exists():
                                fwupd_log.unlink()

    return 0
