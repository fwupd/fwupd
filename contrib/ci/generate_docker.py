#!/usr/bin/env python3
#
# Copyright 2017 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
import os
import subprocess
import sys
import shutil
from fwupd_setup_helpers import parse_dependencies


def get_container_cmd():
    """return docker or podman as container manager"""

    if shutil.which("docker"):
        return "docker"
    if shutil.which("podman"):
        return "podman"


directory = os.path.dirname(sys.argv[0])
TARGET = os.getenv("OS")

if TARGET is None:
    print("Missing OS environment variable")
    sys.exit(1)
OS = TARGET
SUBOS = ""
split = TARGET.split("-")
if len(split) >= 2:
    OS = split[0]
    SUBOS = split[1]

deps = parse_dependencies(OS, SUBOS, False)

f = os.path.join(directory, f"Dockerfile-{OS}.in")
if not os.path.exists(f):
    print(f"Missing input file {f} for {OS}")
    sys.exit(1)

with open(f) as rfd:
    lines = rfd.readlines()

with open("Dockerfile", "w") as wfd:
    for line in lines:
        if line.startswith("FROM %%%ARCH_PREFIX%%%"):
            if (OS == "debian" or OS == "ubuntu") and SUBOS == "i386":
                replace = SUBOS + "/"
            else:
                replace = ""
            wfd.write(line.replace("%%%ARCH_PREFIX%%%", replace))
        elif line == "%%%INSTALL_DEPENDENCIES_COMMAND%%%\n":
            if OS == "fedora":
                wfd.write("RUN dnf --enablerepo=updates-testing -y install \\\n")
            elif OS == "centos":
                wfd.write("RUN yum -y install \\\n")
            elif OS == "debian" or OS == "ubuntu":
                wfd.write("RUN apt update -qq && \\\n")
                wfd.write(
                    "\tDEBIAN_FRONTEND=noninteractive apt install -yq --no-install-recommends\\\n"
                )
            elif OS == "arch":
                wfd.write("RUN pacman -Syu --noconfirm --needed\\\n")
            for i in range(0, len(deps)):
                if i < len(deps) - 1:
                    wfd.write(f"\t{deps[i]} \\\n")
                else:
                    wfd.write(f"\t{deps[i]} || true\n")
        elif line == "%%%ARCH_SPECIFIC_COMMAND%%%\n":
            if OS == "debian" and SUBOS == "s390x":
                # add sources
                wfd.write(
                    'RUN cat /etc/apt/sources.list | sed "s/deb/deb-src/" >> /etc/apt/sources.list\n'
                )
                # add new architecture
                wfd.write(f"RUN dpkg --add-architecture {SUBOS}\n")
        elif line == "%%%OS%%%\n":
            wfd.write(f"ENV OS={TARGET}\n")
        else:
            wfd.write(line)
    wfd.flush()

if len(sys.argv) == 2 and sys.argv[1] == "build":
    cmd = get_container_cmd()
    args = [cmd, "build", "-t", f"fwupd-{TARGET}"]
    if "http_proxy" in os.environ:
        args += [f"--build-arg=http_proxy={os.environ['http_proxy']}"]
    if "https_proxy" in os.environ:
        args += [f"--build-arg=https_proxy={os.environ['https_proxy']}"]
    args += ["-f", "./Dockerfile", "."]
    subprocess.check_call(args)
