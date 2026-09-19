# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'build' subcommand — configure and build fwupd in the venv

import os
import subprocess
from pathlib import Path

from .cli import argparse_func_wrapper
from .directories import directories
from .logger import logger
from .meson import Meson
from .osprofile import RunCmd


def register(subparsers):
    """Register the 'build' subcommand."""
    parser = subparsers.add_parser(
        "build",
        help="build and install fwupd in the venv",
        description=(
            "Configure (meson setup) and build/install fwupd into the build environment. "
            "Extra meson arguments can be passed after '--'."
        ),
    )
    parser.add_argument(
        "meson_args",
        nargs="*",
        help="extra arguments passed to meson setup",
    )
    parser.set_defaults(func=argparse_func_wrapper(run))


def run(args):
    """Build and install fwupd in the venv."""
    extra_args = ["-Dlibxmlb:gtkdoc=false", "-Dsystemd=disabled"]

    # NixOS: extract vendor_ids_dir from mesonFlags
    nixos_marker = directories.build_root() / ".nixos"
    if nixos_marker.exists():
        meson_flags = os.environ.get("mesonFlags", "")
        for flag in meson_flags.split():
            if flag.startswith(("-Dvendor_ids_dir=", "-Dplugin_uefi_capsule_splash=")):
                extra_args.append(flag)

    meson = Meson(
        builddir=directories.builddir(),
        prefix=directories.distdir(absolute=True),
        meson_args=extra_args + args.meson_args,
        cwd=directories.repository_root(),
    )
    if args.quiet:
        meson.capture_logs = True
    if meson.needs_setup:
        if not meson.setup().success:
            return 1

    if not meson.build().success:
        return 1
    if not meson.install(allow_sudo=True).success:
        return 1

    symlink_efi_binaries(directories.distdir())

    return 0


def symlink_efi_binaries(dist: Path):
    """Symlink existing system EFI binaries into the venv dist."""
    try:
        cmd = RunCmd(
            ["pkg-config", "fwupd-efi", "--variable=prefix"],
            check=True,
        )
        efi_prefix = Path(cmd.stdout.strip())
    except (subprocess.CalledProcessError, FileNotFoundError):
        efi_prefix = Path("/usr")

    efi_dir = "libexec/fwupd/efi"
    system_efi = efi_prefix / efi_dir

    if not system_efi.is_dir():
        return

    # Find .efi files
    binaries = []
    for entry in system_efi.glob("*.efi"):
        if entry.is_file():
            binaries.append(entry)

    if not binaries:
        return

    dest_dir = dist / efi_dir
    dest_dir.mkdir(parents=True, exist_ok=True)

    for src in binaries:
        dest = dest_dir / src.name
        if dest.exists() and not dest.is_symlink():
            continue
        if dest.is_symlink():
            dest.unlink()
        dest.symlink_to(src)
        logger.debug(f"Symlinked EFI binary: {dest} -> {src}")
