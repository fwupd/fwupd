# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Meson wrappers for use by fwupbuild

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Self

from .runcmd import RunCmd


class MesonError(Exception):
    """Raised when meson operations fail or version checks don't pass."""


@dataclass(eq=True, order=True)
class MesonVersion:
    major: int
    minor: int
    micro: int
    _pad: int
    rc: int

    def __str__(self) -> str:
        """Return the canonical PEP440-ish form, e.g. '1.11.0' or '1.11.0rc1'."""
        extra = f"rc{self.rc}" if self.rc else ""
        return f"{self.major}.{self.minor}.{self.micro}{extra}"

    @classmethod
    def from_string(cls, ver: str) -> Self:
        """Convert a meson version string to a comparable tuple.

        Handles release candidates: x.y.zrcN (PEP440), x.y.z.rcN or x.y.z-rcN.
        The version is extracted from anywhere in the string, so surrounding
        whitespace, parentheses or trailing text are all tolerated.
        """
        m = re.search(r"(\d+)\.(\d+)\.(\d+)(?:[.-]?rc(\d+))?", ver)
        if not m:
            raise MesonError(f"Unknown meson version format: '{ver}'")
        major, minor, micro = int(m[1]), int(m[2]), int(m[3])
        if m[4] is not None:
            return cls(major, minor, micro, -1, int(m[4]))
        return cls(major, minor, micro, 0, 0)


@dataclass
class Meson:
    builddir: Path
    meson_args: list[str]
    prefix: Path | None = None
    cwd: Path | None = None
    capture_logs: bool = False

    @property
    def needs_setup(self) -> bool:
        return not (self.builddir / "build.ninja").exists()

    def setup(self) -> RunCmd:
        kwargs = {
            "capture": self.capture_logs,
        }
        if self.cwd:
            kwargs["cwd"] = str(self.cwd)

        args = ["--reconfigure"] + self.meson_args

        if self.prefix:
            args.insert(0, f"--prefix={self.prefix}")

        setup_cmd = ["meson", "setup", str(self.builddir)]
        if self.cwd:
            setup_cmd += [self.cwd]  # sourcedir
        setup_cmd += args

        return RunCmd(setup_cmd, **kwargs)

    def build(self) -> RunCmd:
        return RunCmd(
            ["meson", "compile", "-C", self.builddir], capture=self.capture_logs
        )

    def install(self, destdir=None, allow_sudo: bool = False) -> RunCmd:
        kwargs = {
            "capture": self.capture_logs,
        }
        args = []
        if destdir:
            args += ["--destdir", destdir]

        # Bit of a hack: we know if we call meson install the only question
        # it'll ask is:
        # Attempt to use /usr/bin/sudo to gain elevated privileges? [y/n]
        # And we say yes to that, because worst case it'll hang at the
        # sudo password for input afterwards.
        if allow_sudo:
            kwargs["input"] = "y\n"

        return RunCmd(["meson", "install", "-C", self.builddir] + args, **kwargs)

    def test(
        self, test_args: list[str] | None = None, test_env: dict[str, str] | None = None
    ) -> RunCmd:
        return RunCmd(
            ["meson", "test", "-C", self.builddir] + (test_args or []),
            env=test_env,
            capture=False,
        )

    @classmethod
    def get_minimum_meson_version(cls, meson_build: Path) -> MesonVersion:
        with meson_build.open() as f:
            for line in f:
                if "meson_version" in line:
                    m = re.search(r"(\d+\.\d+\.\d+)", line)
                    if m:
                        return MesonVersion.from_string(m.group(1))
        raise MesonError(f"{meson_build} does not contain a required meson version")

    @classmethod
    def current_meson_version(cls) -> MesonVersion:
        """Return the version of the meson that is currently in use.

        Prefers the installed package metadata and falls back to parsing
        ``meson --version`` where meson is not a Python package we can see.
        """
        from importlib.metadata import PackageNotFoundError, version

        try:
            v = version("meson")
        except PackageNotFoundError:
            result = RunCmd(["meson", "--version"])
            if not result.success:
                raise MesonError("Unable to determine the meson version")
            v = result.stdout.strip().split("\n")[0]

        return MesonVersion.from_string(v)

    @classmethod
    def require_version(cls, minimum_version: MesonVersion):
        """Raise a MesonError unless the current meson is at least minimum_version."""
        v = cls.current_meson_version()
        if v < minimum_version:
            raise MesonError(
                f"Minimum version {minimum_version} of meson not met. Have {v}"
            )
