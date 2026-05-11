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
match os.getenv("VARIANT"):
    case str(s) if s.startswith("cross-"):
        CROSS = s.removeprefix("cross-")
        VARIANT = None
    case str(s):
        CROSS = None
        VARIANT = s
    case _:
        CROSS = None
        VARIANT = None


if VARIANT:
    template_file = os.path.join(directory, f"Dockerfile-{DISTRO}-{VARIANT}.in")
else:
    template_file = os.path.join(directory, f"Dockerfile-{DISTRO}.in")
if not os.path.exists(template_file):
    print(f"Missing input file {template_file} for {DISTRO}")
    sys.exit(1)

with open(template_file) as file:
    template = file.read()


match (DISTRO, VARIANT):
    case ("debian", "i386"):
        template = template.replace("FROM debian:testing", "FROM i386/debian:testing")
    case ("debian", "tartan"):
        template = template.replace("FROM debian:testing", "FROM debian:unstable")


if CROSS:
    deps = parse_dependencies(DISTRO, ARCH_TO_DEPS_MAP[CROSS], False, cross=True)
    deps += [f"crossbuild-essential-{CROSS}"]
else:
    deps = parse_dependencies(DISTRO, ARCH_TO_DEPS_MAP[ARCH], False)
deps = sorted(set(deps))
deps = [f"    {i}" for i in deps]
deps = " \\\n".join(deps)

content = template.replace("%%%DEPENDENCIES%%%", deps)

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
