#!/usr/bin/env python3
#
# Copyright 2017 Dell, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from fwupd_setup_helpers import ARCH_TO_DEPS_MAP, parse_dependencies


def replace(content: str, template_vars: dict[str, str]) -> str:
    """
    Replace content using a simple templating system:
       - {{FOO:bar}}: vars[FOO] if set, otherwise "bar"
       - {?FOO-}some{-FOO-}other{-FOO?}: "some" if vars[FOO] is
         set, otherwise "other". The {-FOO-} and other result is optional.
         This instruction works across multiple lines.
         The match can be made more specific using a comparison value:
         {?FOO=="abcd"-}some{-FOO?}
         {?FOO!="abcd"-}some{-FOO?}
    """

    expr_var = r"\{\{(?P<key>\w+)(?::(?P<default>[^}]*))?\}\}"
    expr_block = r'\{\?(?P<key>\w+)(?:(?P<op>==|!=)"(?P<cmpval>[^"]*)")?-\}\n?(?P<if_set>.*?)(?:\{-(?P=key)-\}\n?(?P<if_unset>.*?))?\{-(?P=key)\?\}\n?'

    def replace_var(m: re.Match) -> str:
        key = m.group("key")
        default = m.group("default") if m.group("default") is not None else ""
        return template_vars.get(key, default)

    def replace_block(m: re.Match) -> str:
        key = m.group("key")
        op = m.group("op")
        cmpval = m.group("cmpval")

        match op:
            case None:
                result = key in template_vars
            case "==":
                result = template_vars.get(key) == cmpval
            case "!=":
                result = template_vars.get(key) != cmpval
            case _:
                raise NotImplementedError(f"Invalid op '{op}'")

        if result:
            return m.group("if_set")
        return m.group("if_unset") if m.group("if_unset") is not None else ""

    content = re.sub(expr_block, replace_block, content, flags=re.DOTALL)
    content = re.sub(expr_var, replace_var, content)

    return content


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

    data = {
        "VERSION": version,
        "DISTRO": distro,
    }

    # special cases
    match (distro, variant):
        case ("debian", "i386"):
            data["PLATFORM"] = "linux/i386"
        case _:
            pass

    # insert commands to prepare cross compile
    if cross:
        cross_setup = f"""\
    dpkg --add-architecture {cross};"""
    else:
        cross_setup = "    "
    data["SETUP"] = cross_setup

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
    data["DEPENDENCIES"] = deps

    # install android rust target
    rustup: list[str] = []
    if variant == "android":
        rustup.append("COPY contrib/ci/android.sh .")
        rustup.append("RUN sh android.sh")
    if rustup:
        data["RUSTUP"] = "\n".join(rustup)

    content = replace(content, data)

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
