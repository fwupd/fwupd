# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
#
# 'generate-debian-packaging' subcommand — generate debian packaging files from
# templates

import argparse
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import jinja2

from .dependencies import BuildTarget, DebianDependency, Dependencies
from .osprofile import OsArch, OsName


def register(subparsers):
    """Register the 'generate-debian-packaging' subcommand."""

    debian_parser = subparsers.add_parser(
        "generate-debian-packaging",
        help="Generate Debian packaging files from templates",
    )
    debian_sub = debian_parser.add_subparsers(
        dest="debian_command", help="debian packaging file"
    )
    control_parser = debian_sub.add_parser("control", help="Build the control file")
    control_parser.add_argument(
        "-t",
        "--template",
        type=Path,
        required=True,
        help="The template file to use",
    )
    control_parser.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="The output file to write",
    )
    control_parser.add_argument(
        "--arch",
        type=str,
        default=None,
        help="The arch to use (default: autodetect)",
    )
    control_parser.add_argument(
        "--qubes",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="whether to add Qubes-specific entries",
    )
    control_parser.set_defaults(func=run_control)

    copyright_parser = debian_sub.add_parser(
        "copyright", help="Build the copyright file"
    )
    copyright_parser.add_argument(
        "-t",
        "--template",
        type=Path,
        required=True,
        help="The template file to use",
    )
    copyright_parser.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="The output file to write",
    )
    copyright_parser.set_defaults(func=run_copyright)


def do_the_jinja(template_file: Path, data: dict[str, Any]) -> str:
    loader = jinja2.FileSystemLoader(template_file.parent)
    jinja_env = jinja2.Environment(
        loader=loader,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    jinja_tmpl = jinja_env.get_template(template_file.name)
    return jinja_tmpl.render(data)


@dataclass(order=True, frozen=True)
class DebianPackage:
    name: str
    target: str | None
    version: str | None = None
    inclusive: list[str] = field(default_factory=list)
    exclusive: list[str] = field(default_factory=list)
    profiles: list[str] = field(default_factory=list)

    def __str__(self) -> str:
        s = [self.name]
        if self.target:
            s += [f":{self.target}"]
        if self.version:
            s += [f" {self.version}"]
        if self.inclusive or self.exclusive:
            incl = " ".join(self.inclusive)
            excl = " ".join(f"!{e}" for e in self.exclusive)
            s += [f" [{incl}{' ' if incl and excl else ''}{excl}]"]
        if self.profiles:
            s += [f" <{' '.join(self.profiles)}>"]
        return "".join(s)


def run_control(args):
    dependencies = Dependencies.load()

    host_arch = OsArch.detect()
    arch = args.arch or host_arch
    deps = dependencies.find_packages(
        distro=OsName.DEBIAN,
        filter=arch,
    )

    packages = []
    indep_packages = []
    for dep in deps:
        target = None
        if dep.build_target == BuildTarget.BUILD_INDEPENDENT:
            pass
        elif dep.build_target == BuildTarget.MULTI_ARCH and host_arch != arch:
            target = arch

        if dep.build_target == BuildTarget.BUILD_INDEPENDENT or (
            isinstance(dep, DebianDependency) and dep.control
        ):
            kwargs = {}
            if dep.control:
                kwargs["inclusive"] = dep.control.inclusive
                kwargs["exclusive"] = dep.control.exclusive
                kwargs["profiles"] = dep.control.profiles
                kwargs["version"] = dep.control.version_requirement
            pkg = DebianPackage(name=dep.package_name, target=target, **kwargs)
            if dep.build_target == BuildTarget.BUILD_INDEPENDENT:
                indep_packages.append(pkg)
            else:
                packages.append(pkg)

    data = {
        "QUBES": args.qubes,
        "DEPENDS": sorted(packages),
        "DEPENDS_INDEP": sorted(indep_packages),
    }
    content = do_the_jinja(args.template, data)

    with open(args.output, "w") as fd:
        fd.write(content)
    return 0


def run_copyright(args):
    pattern = r"Copyright\s*[0-9]{4}(\s*-\s*[0-9]{4})?\s*(?P<author>[^<]*)"

    authors = []
    # Need to use iterdir(), this is run from a tarball, not a git tree
    for file in Path("po").iterdir():
        if not file.is_file() or file.name == "COPYING":
            continue

        try:
            with open(file) as fd:
                lines = fd.readlines(220)

            for line in lines:
                if (match := re.search(pattern, line)) is not None:
                    author = match["author"].strip()
                    # Some files only have "Copyright YYYY" line
                    if author:
                        authors.append(author)

        except UnicodeDecodeError:
            continue

    authors = sorted(set(authors))
    data = {"COPYRIGHTS": authors}
    content = do_the_jinja(args.template, data)

    with open(args.output, "w") as fd:
        fd.write(content)
    return 0
