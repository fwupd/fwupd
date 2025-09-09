#!/usr/bin/env python3
#
# Copyright 2017 Dell, Inc.
# Copyright 2020 Intel, Inc.
# Copyright 2021 Mario Limonciello
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
import os
import sys
import argparse

WARNING = "\033[93m"
ENDC = "\033[0m"

# Minimum version of markdown required
MINIMUM_MARKDOWN = (3, 2, 0)


def get_possible_profiles():
    return ["fedora", "centos", "debian", "ubuntu", "arch", "darwin", "freebsd"]


def detect_profile():
    if os.path.exists("/Library/Apple"):
        return "darwin"

    try:
        import distro

        target = distro.id()
        if target == "rhel":
            return "centos"
        if target not in get_possible_profiles():
            target = distro.like()
        return target
    except ModuleNotFoundError:
        pass

    # fallback
    try:
        with open("/etc/os-release", "rb") as f:
            for line in f.read().decode().split("\n"):
                if line.startswith("ID="):
                    target = line[3:].replace('"', "")
                    if target == "rhel":
                        return "centos"
                    return target
    except FileNotFoundError:
        pass

    # failed
    return ""


def pip_install_package(debug, name):
    import subprocess

    cmd = ["python3", "-m", "pip", "install", "--upgrade", name]
    if debug:
        print(cmd)
    subprocess.call(cmd)


def test_jinja2(debug):
    try:
        import jinja2
    except ModuleNotFoundError:
        print("python3-jinja2 must be installed/upgraded")
        pip_install_package(debug, "jinja2")


def test_markdown(debug):
    try:
        import markdown

        new_enough = markdown.__version_info__ >= MINIMUM_MARKDOWN
    except ModuleNotFoundError:
        new_enough = False
    if not new_enough:
        print("python3-markdown must be installed/upgraded")
        pip_install_package(debug, "markdown")


def get_minimum_meson_version():
    import re

    directory = os.path.join(os.path.dirname(sys.argv[0]), "..", "..")

    with open(os.path.join(directory, "meson.build")) as f:
        for line in f:
            if "meson_version" in line:
                return re.search(r"(\d+\.\d+\.\d+)", line).group(1)


def test_meson(debug):
    from importlib.metadata import version, PackageNotFoundError

    minimum = get_minimum_meson_version()
    try:
        new_enough = version("meson") >= minimum
    except PackageNotFoundError:
        import subprocess

        try:
            ver = (
                subprocess.check_output(["meson", "--version"]).strip().decode("utf-8")
            )
            new_enough = ver >= minimum
        except FileNotFoundError:
            new_enough = False
    if not new_enough:
        print("meson must be installed/upgraded")
        pip_install_package(debug, "meson")


def parse_dependencies(OS, variant, add_control):
    import xml.etree.ElementTree as etree

    deps = []
    dep = ""
    directory = os.path.dirname(sys.argv[0])
    tree = etree.parse(os.path.join(directory, "dependencies.xml"))
    root = tree.getroot()
    for child in root:
        if "id" not in child.attrib:
            continue
        for distro in child:
            if "id" not in distro.attrib:
                continue
            if distro.attrib["id"] != OS:
                continue
            control = ""
            if add_control:
                inclusive = []
                exclusive = []
                if not distro.findall("control"):
                    continue
                for control_parent in distro.findall("control"):
                    for obj in control_parent.findall("inclusive"):
                        inclusive.append(obj.text)
                    for obj in control_parent.findall("exclusive"):
                        exclusive.append(obj.text)
                if inclusive or exclusive:
                    inclusive = " ".join(inclusive).strip()
                    exclusive = " !".join(exclusive).strip()
                    if exclusive:
                        exclusive = f"!{exclusive}"
                    control = f" [{inclusive}{exclusive}]"
            for package in distro.findall("package"):
                if variant and "variant" in package.attrib:
                    if package.attrib["variant"] != variant:
                        continue
                if package.text:
                    dep = package.text
                else:
                    dep = child.attrib["id"]
                dep += control
                if dep:
                    deps.append(dep)
    return deps


def _validate_deps(profile: str, deps):
    validated = deps
    if profile == "debian" or profile == "ubuntu":
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


def get_build_dependencies(profile: str, variant: str):
    parsed = parse_dependencies(profile, variant, False)
    return _validate_deps(profile, parsed)


def _get_installer_cmd(profile: str, yes: bool):
    if profile == "darwin":
        return ["brew", "install"]
    if profile in ["debian", "ubuntu"]:
        installer = ["apt", "install"]
    elif profile in ["fedora", "centos"]:
        installer = ["dnf", "install"]
    elif profile == "arch":
        installer = ["pacman", "-Syu", "--noconfirm", "--needed"]
    elif profile == "freebsd":
        installer = ["pkg", "install"]
    else:
        print("unable to detect OS profile, use --os= to specify")
        print(f"\tsupported profiles: {get_possible_profiles()}")
        sys.exit(1)
    if os.geteuid() != 0:
        installer.insert(0, "sudo")
    if yes:
        installer += ["-y"]
    return installer


def install_packages(profile: str, variant: str, yes: bool, debugging: bool, packages):
    import subprocess

    if packages == "build-dependencies":
        packages = get_build_dependencies(profile, variant)
    installer = _get_installer_cmd(profile, yes)
    installer += packages
    if debugging:
        print(installer)
    subprocess.check_call(installer)


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
                "test-jinja2",
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
    elif command == "test-jinja2":
        test_jinja2(args.debug)
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
        if args.os == "darwin":
            install_packages(args.os, args.variant, args.yes, args.debug, ["python"])
        else:
            install_packages(
                args.os, args.variant, args.yes, args.debug, ["python3-pip"]
            )
