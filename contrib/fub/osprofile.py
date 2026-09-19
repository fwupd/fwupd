# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Various utility classes wrapping the OS

import enum
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Self

from .logger import logger
from .runcmd import RunCmd


class UnknownOsException(Exception):
    """
    Raised when an OS is not supported by this tool
    """


@dataclass
class OsRelease:
    """Distribution identification from /etc/os-release."""

    distro: str
    version: str

    @classmethod
    def detect(cls) -> Self:
        """
        Autodetect the OsRelease based on /etc/os-release.

        Raises a FileNotFoundError if the file doesn't exist
        or doesn't include the distribution and version information.
        """
        os_release = Path("/etc/os-release")
        distro = None
        version = None
        with os_release.open() as f:
            for line in f:
                line = line.strip()
                if line.startswith("ID="):
                    distro = line[3:].strip('"')
                elif line.startswith("VERSION_ID="):
                    version = line[11:].strip('"')
        if distro is None or version is None:
            raise FileNotFoundError(os_release)
        return OsRelease(distro, version)


class UnknownArchException(Exception):
    pass


class OsArch(enum.StrEnum):
    """Machine names in their Debian terms"""

    AMD64 = "amd64"
    ARM = "arm"
    ARM64 = "arm64"
    I386 = "i386"
    S390X = "s390x"
    IA64 = "ia64"

    @classmethod
    def detect(cls) -> "OsArch":
        """
        Detect this machine's OsArch
        """
        machine = os.uname().machine
        try:
            return OsArch.from_string(machine)
        except ValueError:
            raise UnknownArchException(f"Unsupported arch '{machine}'")

    @classmethod
    def from_string(cls, arch: str) -> Self:
        """
        Create a an OsArch() from a string. This handles uname/gcc/fedora-style naming.

        Raises a UnknownArchException on failure.
        """
        try:
            return cls(arch)
        except ValueError:
            DEPS_TO_ARCH_MAP = {
                "x86_64": "amd64",
                "armhf": "arm",
                "aarch64": "arm64",
            }
            mapped = DEPS_TO_ARCH_MAP.get(arch)
            if not mapped:
                raise UnknownArchException(f"Unknown arch '{arch}'")
            return cls(mapped)


class OsName(enum.StrEnum):
    """
    A named OS that is known to be supported by us.
    """

    FEDORA = "fedora"
    CENTOS = "centos"
    DEBIAN = "debian"
    UBUNTU = "ubuntu"
    ARCH = "arch"
    DARWIN = "darwin"
    FREEBSD = "freebsd"
    NIXOS = "nixos"

    @classmethod
    def detect(cls) -> Self:
        """
        Detect this machine's OS
        """
        try:
            if sys.platform == "darwin":
                return cls.DARWIN
            if sys.platform.startswith("freebsd"):
                return cls.FREEBSD
            return cls.from_string(OsRelease.detect().distro)
        except (FileNotFoundError, ValueError):
            raise UnknownOsException("Unknown OS name '{name}'")

    @classmethod
    def from_string(cls, name: str) -> Self:
        """
        Map the given string to the OsName
        """
        try:
            if name == "rhel":
                name = "centos"
            return cls(name.lower())
        except ValueError:
            raise UnknownOsException("Unknown OS name '{name}'")

    def package_manager(self) -> "PackageManager":
        cmd = []
        match self:
            case OsName.DARWIN:
                cmd = ["brew", "install"]
            case OsName.DEBIAN | OsName.UBUNTU:
                cmd = ["apt-get", "install", "-q"]
            case OsName.FEDORA | OsName.CENTOS:
                cmd = ["dnf", "install"]
            case OsName.ARCH:
                cmd = ["pacman", "-Syu", "--noconfirm", "--needed"]
            case OsName.FREEBSD:
                cmd = ["pkg", "install"]
            case _:
                raise UnknownOsException(f"PackageManager does not support {self}")

        # brew doesn't allow sudo
        if os.geteuid() != 0 and self != OsName.DARWIN:
            logger.info("Using sudo to install packages")
            cmd.insert(0, "sudo")

        cmd.append("-y")
        return PackageManager(cmd=cmd)


@dataclass
class PackageManager:
    """The OS-specific package manager"""

    cmd: list[str]
    env: dict[str, str] = field(default_factory=dict)

    def install(self, packages: list[str]) -> RunCmd:
        kwargs = {}
        if self.env:
            kwargs["env"] = self.env
        return RunCmd(self.cmd + packages, **kwargs)


@dataclass
class PipPackageManager(PackageManager):
    """
    Wrapper around python's pip
    """

    python: Path = field(default_factory=lambda: PipPackageManager._default_python())

    @staticmethod
    def _default_python() -> Path:
        return Path(sys.executable)

    def install_package(
        self, package: str, version: tuple[int, ...] | None = None
    ) -> RunCmd | None:
        """
        Install a single package with the given version (if any).

        Returns None if no package needs to be installed, a RunCmd of
        the pip install otherwise.
        """
        # Validate package name to prevent code injection
        import re

        if not re.match(r"^[a-zA-Z0-9_-]+$", package):
            raise ValueError(f"Invalid package name: {package}")

        try:
            cmd = RunCmd(
                [self.python, "-c", f"import {package}; print({package}.__version__)"],
            )
            if not cmd.success:
                raise ModuleNotFoundError()
            if version is not None:
                # This will break for packages that use something other than x.y.z,
                # let's fix it then
                package_version = tuple(int(x) for x in cmd.stdout.strip().split("."))
                if package_version < version:
                    raise ModuleNotFoundError()
            return None
        except (ModuleNotFoundError, ValueError):
            logger.debug("Installing/upgrading markdown via pip")
            cmd = RunCmd(
                [self.python, "-m", "pip", "install", "--upgrade", package],
            )
            return cmd

    @classmethod
    def new(cls, python: Path | None) -> Self:
        """
        Returns a new PackageManager for pip packages
        """
        if python is None:
            python = PipPackageManager._default_python()
        cmd = [python, "-m", "pip", "install", "--upgrade"]
        env = os.environ.copy()
        env["PIP_BREAK_SYSTEM_PACKAGES"] = "1"
        return cls(cmd=cmd, env=env, python=python)


@dataclass
class Compiler:
    prog: str

    @property
    def machine(self) -> str:
        result = RunCmd([self.prog, "-dumpmachine"])
        if result.success:
            return result.stdout.strip()
        return ""

    @classmethod
    def get_cc(cls, name: str = "cc") -> Self:
        return cls(prog=name)
