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

# translate debian architecture names (similar to docker/golang names) to the naming in
# the dependencies file, which is closer to gcc/fedora naming.
ARCH_TO_DEPS_MAP = {
    "amd64": "x86_64",
    "arm": "armhf",
    "arm64": "aarch64",
    "i386": "i386",
    "s390x": "s390x",
}


def get_possible_profiles():
    return [
        "fedora",
        "centos",
        "debian",
        "ubuntu",
        "arch",
        "darwin",
        "freebsd",
        "nixos",
    ]


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


def pip_install_package(debug, name, use_pipx=False):
    import shutil
    import subprocess

    env = os.environ.copy()
    if use_pipx and shutil.which("pipx"):
        env["PIPX_HOME"] = "/opt/pipx"
        env["PIPX_BIN_DIR"] = "/usr/bin"
        cmd = ["pipx", "install", "--force", name]
    else:
        env["PIP_BREAK_SYSTEM_PACKAGES"] = "1"
        cmd = ["python3", "-m", "pip", "install", "--upgrade", name]
    if debug:
        print(cmd)
    rc = subprocess.call(cmd, env=env)
    if rc != 0:
        print(f"ERROR: Failed to install {name}")
        sys.exit(1)


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


def _version_tuple(ver):
    import re

    def version_part(s):
        """Returns a tuple with the integer or the string,
        with strings sorthing lower so that 1.rc0 < 1.0
        """
        try:
            return (1, int(s))
        except ValueError:
            return (0, s)

    return tuple(version_part(x) for x in re.split(r"[.\-]", ver))


def test_meson(debug):
    from importlib.metadata import version, PackageNotFoundError

    minimum = get_minimum_meson_version()
    try:
        new_enough = _version_tuple(version("meson")) >= _version_tuple(minimum)
    except PackageNotFoundError:
        import subprocess

        try:
            ver = (
                subprocess.check_output(["meson", "--version"]).strip().decode("utf-8")
            )
            new_enough = _version_tuple(ver) >= _version_tuple(minimum)
        except FileNotFoundError:
            new_enough = False
    if not new_enough:
        print("meson must be installed/upgraded")
        pip_install_package(debug, "meson")


def parse_dependencies(OS, variant, add_control, cross: bool = False):
    import xml.etree.ElementTree as etree

    deps = []
    build_indep = []
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
            version = ""
            is_build_indep = bool(distro.findall("build-indep"))
            if add_control:
                inclusive = []
                exclusive = []
                if not distro.findall("control") and not is_build_indep:
                    continue
                for control_parent in distro.findall("control"):
                    for obj in control_parent.findall("inclusive"):
                        inclusive.append(obj.text)
                    for obj in control_parent.findall("exclusive"):
                        exclusive.append(obj.text)
                    for obj in control_parent.findall("version"):
                        if obj.text:
                            version = f" {obj.text}"
                if inclusive or exclusive:
                    inclusive = " ".join(inclusive).strip()
                    exclusive = " !".join(exclusive).strip()
                    if exclusive:
                        exclusive = f"!{exclusive}"
                    control = f" [{inclusive}{exclusive}]"

            if cross and distro.findall("multi-arch"):
                deb_arch = {v: k for k, v in ARCH_TO_DEPS_MAP.items()}.get(
                    variant, variant
                )
                arch_suffix = f":{deb_arch}"
            elif distro.findall("native"):
                arch_suffix = ":native"
            else:
                arch_suffix = ""
            if len(distro.findall("package")) == 0:
                dep = child.attrib["id"]
                if dep:
                    if is_build_indep:
                        build_indep.append(f"{dep}{arch_suffix}{version}{control}")
                    else:
                        deps.append(f"{dep}{arch_suffix}{version}{control}")
            for package in distro.findall("package"):
                if variant and "variant" in package.attrib:
                    if package.attrib["variant"] != variant:
                        continue
                if package.text:
                    dep = package.text
                else:
                    dep = child.attrib["id"]
                if dep:
                    if is_build_indep:
                        build_indep.append(f"{dep}{arch_suffix}{version}{control}")
                    else:
                        deps.append(f"{dep}{arch_suffix}{version}{control}")
    return deps, build_indep


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


def get_build_dependencies(profile: str, variant: str, cross: bool = False):
    parsed, build_indep = parse_dependencies(profile, variant, False, cross)
    return _validate_deps(profile, parsed + build_indep)


def _get_installer_cmd(profile: str, yes: bool):
    if profile == "darwin":
        return ["brew", "install"]
    if profile in ["debian", "ubuntu"]:
        installer = ["apt-get", "install", "-qq"]
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


def install_packages(
    profile: str,
    variant: str,
    yes: bool,
    debugging: bool,
    packages,
    cross: bool = False,
):
    import subprocess

    if profile == "nixos":
        return
    if packages == "build-dependencies":
        packages = get_build_dependencies(profile, variant, cross)
    installer = _get_installer_cmd(profile, yes)
    installer += packages
    if debugging:
        print(installer)
    try:
        subprocess.check_output(installer, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        print("ERROR: Failed to install packages:")
        print(e.output.decode("utf-8"))
        sys.exit(1)


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
        "--cross",
        action="store_true",
        help="indicate that variant describes a cross build target",
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

    args.variant = ARCH_TO_DEPS_MAP.get(args.variant, args.variant)

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
        dependencies = get_build_dependencies(args.os, args.variant, args.cross)
        print(*dependencies, sep="\n")
    elif command == "install-dependencies":
        install_packages(
            args.os,
            args.variant,
            args.yes,
            args.debug,
            "build-dependencies",
            args.cross,
        )
    elif command == "install-pip":
        if args.os == "darwin":
            install_packages(args.os, args.variant, args.yes, args.debug, ["python"])
        elif args.os == "freebsd":
            install_packages(args.os, args.variant, args.yes, args.debug, ["py312-pip"])
        elif args.os == "arch":
            install_packages(
                args.os,
                args.variant,
                args.yes,
                args.debug,
                ["python-pip", "python-pipx"],
            )
        elif args.os == "centos":
            install_packages(
                args.os, args.variant, args.yes, args.debug, ["python3-pip"]  # no pipx
            )
        else:
            install_packages(
                args.os, args.variant, args.yes, args.debug, ["python3-pip", "pipx"]
            )
