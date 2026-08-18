#!/usr/bin/env python3
#
# Copyright 2017 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from fwupd_setup_helpers import ARCH_TO_DEPS_MAP, parse_dependencies


def get_container_cmd():
    """return docker or podman as container manager"""

    if shutil.which("docker"):
        return "docker"
    if shutil.which("podman"):
        return "podman"


def generate_dockerfile(
    distro: str, version: str, arch: str, variant: str | None
) -> str:
    """Generate a Dockerfile from the template for the given distro/version/arch/variant."""

    directory = os.path.dirname(sys.argv[0])
    cross = (
        c if variant and (c := str(variant).removeprefix("cross-")) != variant else None
    )

    # find first existing
    dockerfiles = [
        Path(directory) / f"Dockerfile-{distro}-{variant}.in",
        Path(directory) / f"Dockerfile-{distro}.in",
    ]
    try:
        template_file = next(p for p in dockerfiles if p.exists())
    except StopIteration:
        raise FileNotFoundError(f"Missing template Dockerfile for {distro}") from None
    with open(template_file) as file:
        content = file.read()

    content = content.replace("%%%VERSION%%%", version)

    # special cases
    match (distro, variant):
        case ("debian", "i386"):
            content = content.replace(
                f"FROM debian:{version}", f"FROM i386/debian:{version}"
            )

    # insert commands to prepare cross compile
    if cross:
        cross_setup = f"""\
    sed -i 's|Types: deb|Types: deb deb-src|' /etc/apt/sources.list.d/debian.sources; \\
    dpkg --add-architecture {cross};"""
    else:
        cross_setup = "    "
    content = content.replace("%%%SETUP%%%", cross_setup)

    # insert dependencies to install
    if cross:
        deps_parsed, build_indep = parse_dependencies(
            distro, ARCH_TO_DEPS_MAP[cross], False, cross=True
        )
        deps = deps_parsed + build_indep + [f"crossbuild-essential-{cross}"]
    elif variant in ["i386", "android"]:
        deps_parsed, build_indep = parse_dependencies(distro, variant, False)
        deps = deps_parsed + build_indep
    else:
        deps_parsed, build_indep = parse_dependencies(
            distro, ARCH_TO_DEPS_MAP[arch], False
        )
        deps = deps_parsed + build_indep
    deps = sorted(set(deps))
    deps = [f"    {i}" for i in deps]
    deps = " \\\n".join(deps)
    content = content.replace("%%%DEPENDENCIES%%%", deps)

    # install android rust target
    rustup: list[str] = []
    if variant == "android":
        rustup.append("COPY contrib/ci/android.sh .")
        rustup.append("RUN sh android.sh")
    content = content.replace("%%%RUSTUP%%%", "\n".join(rustup))

    return content


parser = argparse.ArgumentParser(
    description="Generate and optionally build a Dockerfile for CI"
)
parser.add_argument(
    "--distro", required=True, help="Distribution name (e.g. fedora, debian)"
)
parser.add_argument("--arch", default="amd64", help="Architecture (e.g. amd64, arm64)")
parser.add_argument(
    "--version",
    required=True,
    help="Distribution version/tag (e.g. 44, testing, rolling)",
)
parser.add_argument(
    "--variant", default=None, help="Build variant (e.g. i386, android, cross-s390x)"
)

subparsers = parser.add_subparsers(dest="command")
subparsers.add_parser(
    "build", help="Build the container image after generating the Dockerfile"
)

args = parser.parse_args()

content = generate_dockerfile(args.distro, args.version, args.arch, args.variant)

with open("Dockerfile", "w") as file:
    file.write(content)

if args.command == "build":
    cmd = get_container_cmd()
    build_args = [cmd, "build", "-t", f"fwupd-{args.distro}"]
    if http_proxy := os.environ.get("http_proxy"):
        build_args += [f"--build-arg=http_proxy={http_proxy}"]
    if https_proxy := os.environ.get("https_proxy"):
        build_args += [f"--build-arg=https_proxy={https_proxy}"]
    build_args += ["-f", "./Dockerfile", "."]
    subprocess.check_call(build_args)
