# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'docker' subcommand — generate and optionally build a Dockerfile for CI

import os
from pathlib import Path

from .cli import argparse_func_wrapper
from .directories import directories
from .docker import Dockerfile
from .logger import logger, printer
from .osprofile import (
    OsArch,
    OsName,
    OsRelease,
    UnknownArchException,
    UnknownOsException,
)
from .runcmd import RunCmd

VALID_ENGINES = ("docker", "podman")


def register(subparsers):
    """Register the 'dockerfile' subcommand."""
    parser = subparsers.add_parser(
        "dockerfile",
        help="generate and optionally build a CI Dockerfile",
        description=(
            "Generate a Dockerfile from a CI template for the given "
            "distro/version/arch, and optionally build the container image."
        ),
    )
    parser.add_argument(
        "--os",
        dest="os_name",
        choices=list(OsName),
        default=None,
        help="distribution name (default: auto-detect)",
    )
    parser.add_argument(
        "--version",
        default=None,
        help="distribution version/tag (default: auto-detect)",
    )
    parser.add_argument(
        "--arch",
        type=str,
        default=None,
        help="architecture (default: auto-detect)",
    )
    parser.add_argument(
        "--variant",
        default=None,
        help="build variant (e.g. i386, android, cross-s390x)",
    )
    parser.add_argument(
        "--print",
        action="store_true",
        default=False,
        help="Print the generated Dockerfile, do not write to disk",
    )
    parser.add_argument(
        "-F",
        dest="dockerfile",
        type=Path,
        default=Path("Dockerfile"),
        help="output Dockerfile path (default: Dockerfile)",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        default=False,
        help="build the container image after generating the Dockerfile",
    )
    parser.add_argument(
        "--engine",
        choices=VALID_ENGINES,
        default=None,
        help="container engine to use for --build (default: auto-detect)",
    )
    parser.add_argument(
        "--tag",
        default=None,
        help="image tag for --build (default: fwupd-DISTRO)",
    )
    parser.set_defaults(func=argparse_func_wrapper(run))


def find_container_engine(engine: str | None) -> str:
    """Find the container engine binary.

    If engine is specified, use that. Otherwise auto-detect docker or podman.
    Returns the engine name, raises FileNotFoundError if not found.
    """
    engines = [engine] if engine is not None else VALID_ENGINES
    for candidate in engines:
        cmd = RunCmd([candidate, "--help"])
        if cmd.success:
            return candidate

    raise FileNotFoundError


def run(args):
    """Generate and optionally build a CI Dockerfile."""

    try:
        if args.os_name:
            osname = OsName.from_string(args.os_name)
            if not args.version:
                printer.error("--version is required if --os is given")
                return 2
            version = args.version
        else:
            osname = OsName.detect()
            version = args.version or OsRelease.detect().version
    except (UnknownOsException, FileNotFoundError):
        profiles = " ".join(f"'{p}'" for p in OsName)
        printer.error(
            f"Could not detect OS profile. Use --os to specify one of {profiles}."
        )
        return 1

    try:
        if args.arch:
            arch = OsArch.from_string(args.arch)
        else:
            arch = OsArch.detect()
    except UnknownArchException:
        printer.error("Unknown arch, cannot continue")
        return 1

    try:
        cross_id = (
            c
            if args.variant
            and (c := str(args.variant).removeprefix("cross-")) != args.variant
            else None
        )
        if cross_id:
            cross = OsArch.from_string(cross_id)
        else:
            cross = None
    except UnknownArchException as e:
        printer.error(f"Failed to find cross arch: {e}")
        return 1

    repo_root = directories.repository_root()
    ci_dir = repo_root / "contrib" / "ci"
    dockerfile = Dockerfile(
        distro=osname,
        version=version,
        arch=arch,
        variant=args.variant,
        cross_arch=cross,
    )

    try:
        content = dockerfile.generate_from_template(ci_dir)
        if not args.print:
            for line in content.split("\n"):
                logger.debug(line)
    except FileNotFoundError as e:
        printer.error(str(e))
        return 1

    if args.print:
        print(content)
        return 0

    with open(args.dockerfile, "w") as file:
        file.write(content)
    logger.info(f"Generated '{args.dockerfile}'")

    if not args.build:
        return 0

    try:
        engine = find_container_engine(args.engine)
    except FileNotFoundError:
        if args.engine:
            printer.error(f"{args.engine} not found")
        else:
            engines = ", ".join(VALID_ENGINES)
            printer.error(f"No container engine found (tried {engines})")
        return 1

    logger.info(f"Using container engine: {engine}")

    tag = args.tag if args.tag else f"fwupd-{osname}"
    build_cmd = [engine, "build", "-t", tag]

    http_proxy = os.environ.get("http_proxy")
    if http_proxy:
        build_cmd += [f"--build-arg=http_proxy={http_proxy}"]
    https_proxy = os.environ.get("https_proxy")
    if https_proxy:
        build_cmd += [f"--build-arg=https_proxy={https_proxy}"]

    build_cmd += ["-f", str(args.dockerfile.resolve()), "."]

    printer.message(f"Building container image {tag}")
    result = RunCmd(build_cmd, capture=False, cwd=directories.repository_root())
    if not result.success:
        printer.error("Container build failed")
        return 1

    return 0
