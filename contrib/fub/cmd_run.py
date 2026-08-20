# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'run' subcommand — run fwupdtool/fwupdmgr/fwupd from the venv

import os
import shutil
import sys
from pathlib import Path

from .cli import argparse_func_wrapper
from .directories import directories
from .logger import logger, printer
from .meson import Meson
from .osprofile import Compiler, RunCmd

VALID_BINARIES = ("fwupdtool", "fwupdmgr", "fwupd")

DBUSPOLICY = Path("/usr/share/dbus-1/system.d/org.freedesktop.fwupd.conf")
PKPOLICY = Path("/usr/share/polkit-1/actions/org.freedesktop.fwupd.policy")


def register(subparsers):
    """Register the 'run' subcommand."""
    parser = subparsers.add_parser(
        "run",
        help="run fwupdtool, fwupdmgr, or fwupd from the venv",
        description=(
            "Run one of the fwupd binaries from the venv build. "
            "Set DEBUG=1 in the environment to launch via gdbserver."
            "To separate fub arguments from tool arguments, use --, "
            "e.g. fub run -- fwupdtool --help"
        ),
    )
    parser.add_argument(
        "binary",
        choices=VALID_BINARIES,
        help="binary to run",
    )
    parser.add_argument(
        "args",
        nargs="*",
        help="arguments to pass to the binary",
    )
    parser.set_defaults(func=argparse_func_wrapper(run))


def run(args):
    """Run a fwupd binary from the venv."""

    meson = Meson(builddir=directories.builddir(), meson_args=[])
    if meson.needs_setup:
        printer.error("Project not yet built, run [bold]fub build[/bold] first")
        sys.exit(1)

    dist = directories.distdir()
    binary = args.binary
    binary_args = args.args

    machine = Compiler.get_cc().machine

    # Find the executable
    exe = dist / "libexec" / "fwupd" / binary
    if not exe.is_file():
        exe = dist / "bin" / binary
    if not exe.is_file():
        printer.error(
            f"{binary} is not yet built, please run [bold]fub build[/bold] first",
        )
        return 1

    # Set up environment variables
    env_vars = {
        "FWUPD_LOCALSTATEDIR": str(dist),
        "FWUPD_SYSCONFDIR": str(dist / "etc"),
        "FWUPD_POLKIT_NOCHECK": "1",
    }

    ld_paths = []
    if machine:
        ld_paths.append(str(dist / "lib" / machine))
    ld_paths.extend(
        [
            str(dist / "lib64"),
            str(dist / "lib"),
        ]
    )
    env_vars["LD_LIBRARY_PATH"] = ":".join(ld_paths)
    env_vars["G_DEBUG"] = os.environ.get("G_DEBUG", "fatal-criticals")
    env_vars["GLIBC_TUNABLES"] = os.environ.get(
        "GLIBC_TUNABLES", "glibc.cpu.hwcaps=SHSTK"
    )

    # Forward FWUPD_* environment variables
    for key, value in os.environ.items():
        if key.startswith("FWUPD"):
            env_vars[key] = value

    # Forward other specific env vars
    for key in ("TPM2TOOLS_TCTI",):
        if key in os.environ:
            env_vars[key] = os.environ[key]

    # Debug mode: use gdbserver
    debug_prefix = []
    if os.environ.get("DEBUG") == "1":
        if not shutil.which("gdbserver"):
            printer.error("Install gdbserver to enable debugging")
            return 1
        debug_prefix = ["gdbserver", "localhost:9091"]

    sudo = shutil.which("sudo")
    if not sudo:
        printer.error("sudo is required to run fwupd binaries")
        return 1

    chown_files: list[Path] = []
    match binary:
        case "fwupd":
            if not check_dbus_policy(dist, sudo):
                return 1
        case "fwupdmgr":
            if not check_polkit_policy(dist, sudo):
                return 1

            # Special handling: chown emulation-save output
            if len(binary_args) >= 2 and binary_args[0] == "emulation-save":
                output_path = Path(binary_args[1])
                # Validate path is within dist directory to prevent path traversal
                try:
                    output_path.resolve().relative_to(dist.resolve())
                    chown_files.append(output_path)
                except ValueError:
                    printer.error(
                        f"Output file must be within build directory: {output_path}"
                    )
                    return 1

    cmd = debug_prefix + [exe] + binary_args
    result = RunCmd(cmd, capture=False, as_sudo=True, sudo_env=env_vars)

    for output_file in chown_files:
        if output_file.exists():
            uid = os.getuid()
            gid = os.getgid()
            RunCmd([sudo, "chown", "--no-dereference", f"{uid}:{gid}", output_file])

    return result.returncode


def check_dbus_policy(dist, sudo) -> bool:
    """Check for D-Bus policy and offer to install it."""
    if not DBUSPOLICY.parent.is_dir():
        return True
    if DBUSPOLICY.is_file():
        return True

    logger.info(f"Missing D-Bus policy in {DBUSPOLICY}")
    if not printer.ask_yn(f"Install D-Bus policy file to {DBUSPOLICY.parent}/? [y/N] "):
        return True

    src = dist / "share" / "dbus-1" / "system.d" / "org.freedesktop.fwupd.conf"
    cmd = RunCmd([sudo, "cp", src, DBUSPOLICY])
    if not cmd.success:
        printer.error("Failed to install DBus policy file")
        return False

    return True


def check_polkit_policy(dist, sudo) -> bool:
    """Check for PolicyKit policy and offer to install it."""
    if not PKPOLICY.parent.is_dir():
        return True

    needs_update = False
    if not PKPOLICY.is_file():
        needs_update = True
    else:
        if "org.freedesktop.fwupd.emulation-save" not in PKPOLICY.read_text():
            needs_update = True

    if not needs_update:
        return True

    logger.info(f"Missing or outdated PolicyKit policy in {PKPOLICY}")
    if not printer.ask_yn(f"Copy PolicyKit policy into {PKPOLICY.parent}/? [y/N] "):
        return False

    src = dist / "share" / "polkit-1" / "actions" / "org.freedesktop.fwupd.policy"
    cmd = RunCmd([sudo, "cp", src, PKPOLICY])
    if not cmd.success:
        printer.error("Failed to install PolicyKit policy")

    return cmd.success
