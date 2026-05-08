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
from typing import Optional

from fwupd_setup_helpers import parse_dependencies


# this is idiosyncratic for an amazing reason
RUNNER_ARCH_DEPS_MAP = {
    "ARM": "armhf",
    "ARM64": "aarch64",
    "X64": "x86_64",
    "X86": "i386",
}


def getenv_unwrap(name: str) -> Optional[str]:
    val = os.getenv(name)
    if val is None:
        print(f"environment variable has not been set: '{name}'")
        sys.exit(1)
    if val.lower() in ["null", "none"]:
        return None
    return val


def get_container_cmd():
    """return docker or podman as container manager"""

    if shutil.which("docker"):
        return "docker"
    if shutil.which("podman"):
        return "podman"


directory = os.path.dirname(sys.argv[0])
MATRIX_CROSS = getenv_unwrap("MATRIX_CROSS")
RUNNER_ARCH = getenv_unwrap("RUNNER_ARCH")
TARGET_DISTRO = getenv_unwrap("TARGET_DISTRO")

template_file = os.path.join(directory, f"Dockerfile-{TARGET_DISTRO}.in")
if not os.path.exists(template_file):
    print(f"Missing input file {template_file} for {TARGET_DISTRO}")
    sys.exit(1)

with open(template_file) as file:
    template = file.read()


# special case for i386-based debian container
match TARGET_DISTRO:
    case "debian-i386":
        template = template.replace("FROM debian:testing", "FROM i386/debian:testing")
    case "debian-tartan":
        template = template.replace("FROM debian:testing", "FROM debian:unstable")


distro = TARGET_DISTRO.split("-")[0]
if MATRIX_CROSS:
    deps = parse_dependencies(distro, MATRIX_CROSS, False, cross=True)
    deps += [f"crossbuild-essential-{MATRIX_CROSS}"]
else:
    deps = parse_dependencies(distro, RUNNER_ARCH_DEPS_MAP[RUNNER_ARCH], False)
deps = sorted(set(deps))
deps = [f"    {i}" for i in deps]
deps = " \\\n".join(deps)

content = template.replace("%%%DEPENDENCIES%%%", deps)

with open("Dockerfile", "w") as file:
    file.write(content)

if len(sys.argv) == 2 and sys.argv[1] == "build":
    cmd = get_container_cmd()
    args = [cmd, "build", "-t", f"fwupd-{TARGET_DISTRO}"]
    if "http_proxy" in os.environ:
        args += [f"--build-arg=http_proxy={os.environ['http_proxy']}"]
    if "https_proxy" in os.environ:
        args += [f"--build-arg=https_proxy={os.environ['https_proxy']}"]
    args += ["-f", "./Dockerfile", "."]
    subprocess.check_call(args)
