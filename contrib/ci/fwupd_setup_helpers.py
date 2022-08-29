#!/usr/bin/python3
#
# Copyright (C) 2017 Dell, Inc.
# Copyright (C) 2020 Intel, Inc.
# Copyright (C) 2021 Mario Limonciello
#
# SPDX-License-Identifier: LGPL-2.1+
#
import os
import sys
import argparse

WARNING = "\033[93m"
ENDC = "\033[0m"

# Minimum version of markdown required
MINIMUM_MARKDOWN = (3, 3, 3)

# Minimum meson required
MINIMUM_MESON = "0.61.0"


def get_possible_profiles():
    return ["fedora", "centos", "debian", "ubuntu", "arch", "void"]


def detect_profile():
    try:
        import distro

        target = distro.id()
        if not target in get_possible_profiles():
            target = distro.like()
    except ModuleNotFoundError:
        target = ""
    return target


def pip_install_package(debug, name):
    import subprocess

    cmd = ["python3", "-m", "pip", "install", "--upgrade", name]
    if debug:
        print(cmd)
    subprocess.call(cmd)


def test_markdown(debug):
    try:
        import markdown

        new_enough = markdown.__version_info__ >= MINIMUM_MARKDOWN
    except ModuleNotFoundError:
        new_enough = False
    if not new_enough:
        print("python3-markdown must be installed/upgraded")
        pip_install_package(debug, "markdown")


def test_meson(debug):
    from importlib.metadata import version, PackageNotFoundError

    try:
        new_enough = version("meson") >= MINIMUM_MESON
    except PackageNotFoundError:
        import subprocess

        try:
            ver = (
                subprocess.check_output(["meson", "--version"]).strip().decode("utf-8")
            )
            new_enough = ver >= MINIMUM_MESON
        except FileNotFoundError:
            new_enough = False
    if not new_enough:
        print("meson must be installed/upgraded")
        pip_install_package(debug, "meson")


def parse_dependencies(OS, variant, requested_type):
    import xml.etree.ElementTree as etree

    deps = []
    dep = ""
    directory = os.path.dirname(sys.argv[0])
    tree = etree.parse(os.path.join(directory, "dependencies.xml"))
    root = tree.getroot()
    for child in root:
        if "type" not in child.attrib or "id" not in child.attrib:
            continue
        for distro in child:
            if "id" not in distro.attrib:
                continue
            if distro.attrib["id"] != OS:
                continue
            packages = distro.findall("package")
            for package in packages:
                if variant:
                    if "variant" not in package.attrib:
                        continue
                    if package.attrib["variant"] != variant:
                        continue
                if package.text:
                    dep = package.text
                else:
                    dep = child.attrib["id"]
                if child.attrib["type"] == requested_type and dep:
                    deps.append(dep)
    return deps


def _validate_deps(os, deps):
    validated = deps
    if os == "debian" or os == "ubuntu":
        try:
            from apt import cache

            cache = cache.Cache()
            for pkg in deps:
                if not cache.has_key(pkg) and not cache.is_virtual_package(pkg):
                    print(
                        f"{WARNING}WARNING:{ENDC} ignoring unavailable package %s" % pkg
                    )
                    validated.remove(pkg)
        except ModuleNotFoundError:
            print(
                f"{WARNING}WARNING:{ENDC} Unable to validate package dependency list without python3-apt"
            )
    return validated


def get_build_dependencies(os, variant):
    parsed = parse_dependencies(os, variant, "build")
    return _validate_deps(os, parsed)


def _get_installer_cmd(os, yes):
    if os == "debian" or os == "ubuntu":
        installer = ["apt", "install"]
    elif os == "fedora":
        installer = ["dnf", "install"]
    elif os == "arch":
        installer = ["pacman", "-Syu", "--noconfirm", "--needed"]
    elif os == "void":
        installer = ["xbps-install", "-Syu"]
    else:
        print("unable to detect OS profile, use --os= to specify")
        print("\tsupported profiles: %s" % get_possible_profiles())
        sys.exit(1)
    if yes:
        installer += ["-y"]
    return installer


def install_packages(os, variant, yes, debugging, packages):
    import subprocess

    if packages == "build-dependencies":
        packages = get_build_dependencies(os, variant)
    installer = _get_installer_cmd(os, yes)
    installer += packages
    if debugging:
        print(installer)
    subprocess.call(installer)


if __name__ == "__main__":

    command = None
    # compat mode for old training documentation
    if "generate_dependencies.py" in sys.argv[0]:
        command = "get-dependencies"

    parser = argparse.ArgumentParser()
    if not command:
        parser.add_argument(
            "command",
            choices=[
                "get-dependencies",
                "test-markdown",
                "test-meson",
                "detect-profile",
                "install-dependencies",
                "install-pip",
            ],
            help="command to run",
        )
    parser.add_argument(
        "-o",
        "--os",
        default=detect_profile(),
        choices=get_possible_profiles(),
        help="calculate dependencies for OS profile",
    )
    parser.add_argument(
        "-v", "--variant", help="optional machine variant for the OS profile"
    )
    parser.add_argument(
        "-y", "--yes", action="store_true", help="Don't prompt to install"
    )
    parser.add_argument(
        "-d", "--debug", action="store_true", help="Display all launched commands"
    )
    args = parser.parse_args()

    # fall back in all cases
    if not args.variant:
        args.variant = os.uname().machine
    if not command:
        command = args.command

    # command to run
    if command == "test-markdown":
        test_markdown(args.debug)
    elif command == "test-meson":
        test_meson(args.debug)
    elif command == "detect-profile":
        print(detect_profile())
    elif command == "get-dependencies":
        dependencies = get_build_dependencies(args.os, args.variant)
        print(*dependencies, sep="\n")
    elif command == "install-dependencies":
        install_packages(
            args.os, args.variant, args.yes, args.debug, "build-dependencies"
        )
    elif command == "install-pip":
        install_packages(args.os, args.variant, args.yes, args.debug, ["python3-pip"])
