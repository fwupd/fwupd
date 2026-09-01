# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'build' subcommand — configure and build fwupd in the venv

import os
import subprocess
from pathlib import Path

from .helpers import (
    build_path,
    dist_path,
    find_repo_root,
    logger,
    require_venv,
    venv_path,
    Meson,
    RunCmd,
)


def register(subparsers):
    """Register the 'build' subcommand."""
    parser = subparsers.add_parser(
        "build",
        help="build and install fwupd in the venv",
        description=(
            "Configure (meson setup) and build/install fwupd into the venv. "
            "Extra meson arguments can be passed after '--'."
        ),
    )
    parser.add_argument(
        "meson_args",
        nargs="*",
        help="extra arguments passed to meson setup",
    )
    parser.set_defaults(func=run)


def run(args):
    """Build and install fwupd in the venv."""
    require_venv(args)

    root = find_repo_root()
    build = build_path(args)
    dist = dist_path(args)
    venv = venv_path(args)

    extra_args = ["-Dlibxmlb:gtkdoc=false", "-Dsystemd=disabled"]

    # NixOS: extract vendor_ids_dir from mesonFlags
    nixos_marker = venv / ".nixos"
    if nixos_marker.exists():
        meson_flags = os.environ.get("mesonFlags", "")
        for flag in meson_flags.split():
            if flag.startswith(("-Dvendor_ids_dir=", "-Dplugin_uefi_capsule_splash=")):
                extra_args.append(flag)

    meson = Meson(
        builddir=build, prefix=dist, meson_args=extra_args + args.meson_args, cwd=root
    )
    if meson.needs_setup:
        if not meson.setup().success:
            return 1

    meson.build()
    meson.install()

    symlink_efi_binaries(dist)

    return 0


def symlink_efi_binaries(dist):
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
