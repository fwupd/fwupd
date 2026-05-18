#!/usr/bin/env python3
#
# Copyright 2017 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import os
import shutil
import subprocess
import sys
from pathlib import Path

from fwupd_setup_helpers import parse_dependencies

# translate debian architecture names (similar to docker/golang names) to the naming in the
# dependencies file, which is closer to gcc/fedora naming.
ARCH_TO_DEPS_MAP = {
    "amd64": "x86_64",
    "arm": "armhf",
    "arm64": "aarch64",
    "i386": "i386",
    "s390x": "s390x",
}


def getenv_unwrap(name: str) -> str:
    val = os.getenv(name)
    if val is None:
        print(f"environment variable has not been set: '{name}'")
        sys.exit(1)
    return val


def get_container_cmd():
    """return docker or podman as container manager"""

    if shutil.which("docker"):
        return "docker"
    if shutil.which("podman"):
        return "podman"


directory = os.path.dirname(sys.argv[0])
DISTRO = getenv_unwrap("DISTRO")
ARCH = getenv_unwrap("ARCH")
VARIANT = os.getenv("VARIANT")
CROSS = c if (c := str(VARIANT).removeprefix("cross-")) != VARIANT else None


# find first existing
dockerfiles = [
    Path(directory) / f"Dockerfile-{DISTRO}-{VARIANT}.in",
    Path(directory) / f"Dockerfile-{DISTRO}.in",
]
try:
    template_file = next(p for p in dockerfiles if p.exists())
except StopIteration:
    raise FileNotFoundError("Missing template Dockerfile for {DISTRO}") from None
with open(template_file) as file:
    content = file.read()


# special cases
match (DISTRO, VARIANT):
    case ("debian", "i386"):
        content = content.replace("FROM debian:testing", "FROM i386/debian:testing")


# insert commands to prepare cross compile
if CROSS:
    cross_setup = f"""\
    sed -i 's|Types: deb|Types: deb deb-src|' /etc/apt/sources.list.d/debian.sources; \\
    dpkg --add-architecture {CROSS};"""
else:
    cross_setup = "    "
content = content.replace("%%%SETUP%%%", cross_setup)


# insert dependencies to install
if CROSS:
    deps = parse_dependencies(DISTRO, ARCH_TO_DEPS_MAP[CROSS], False, cross=True)
    deps += [f"crossbuild-essential-{CROSS}"]
elif VARIANT == "i386":
    deps = parse_dependencies(DISTRO, VARIANT, False)
else:
    deps = parse_dependencies(DISTRO, ARCH_TO_DEPS_MAP[ARCH], False)
deps = sorted(set(deps))
deps = [f"    {i}" for i in deps]
deps = " \\\n".join(deps)
content = content.replace("%%%DEPENDENCIES%%%", deps)


with open("Dockerfile", "w") as file:
    file.write(content)

if len(sys.argv) == 2 and sys.argv[1] == "build":
    cmd = get_container_cmd()
    args = [cmd, "build", "-t", f"fwupd-{DISTRO}"]
    if "http_proxy" in os.environ:
        args += [f"--build-arg=http_proxy={os.environ['http_proxy']}"]
    if "https_proxy" in os.environ:
        args += [f"--build-arg=https_proxy={os.environ['https_proxy']}"]
    args += ["-f", "./Dockerfile", "."]
    subprocess.check_call(args)
