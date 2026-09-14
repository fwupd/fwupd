# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Build/repo directory management for fub

from dataclasses import dataclass
from pathlib import Path
from typing import ClassVar, Self


@dataclass
class Directories:
    _repository_root: Path
    _build_root: Path

    MARKERFILE: ClassVar[str] = "this-is-a-fwupd-build-root"

    @classmethod
    def find_repo_root(cls) -> Path:
        """Find the first parent directory containing meson_options.txt"""
        parent = Path(__file__).absolute().parent
        while parent != parent.parent:
            if (parent / "meson_options.txt").exists():
                return parent
            parent = parent.parent

        raise FileNotFoundError("Unable to find repository root")

    @classmethod
    def find_most_recent_build_root(cls) -> Path | None:
        """Find the most recently created venv by looking for the marker file.

        Returns the directory containing the most recent marker file, or
        None if no marker file is found.
        """
        root = cls.find_repo_root()
        candidates = sorted(root.rglob(cls.MARKERFILE), key=lambda p: p.stat().st_mtime)
        if candidates:
            return candidates[-1].parent
        return None

    def repository_root(self, absolute: bool = False) -> Path:
        """
        The git repository root directory
        """
        if not absolute:
            return self._repository_root
        return Path(self._repository_root).absolute()

    def build_root(self, absolute: bool = False) -> Path:
        """
        The build root directory
        """
        assert self._build_root is not None, "Bug: the build root must be set"
        builddir = self._build_root
        return self.repository_root() / builddir if absolute else builddir

    def builddir(self, absolute: bool = False) -> Path:
        """
        The meson builddir
        """
        assert self.build_root is not None, "Bug: the build root must be set"
        return self.build_root(absolute) / "build"

    def distdir(self, absolute: bool = False) -> Path:
        """
        The meson distdir
        """
        assert self.build_root is not None, "Bug: the build root must be set"
        return self.build_root(absolute) / "dist"

    def python(self) -> Path:
        assert self._build_root is not None, "Bug: the build root must be set"
        python = self._build_root / "bin" / "python"
        assert python.exists(), "Bug: python is not available"
        return python

    @property
    def build_root_is_initialized(self) -> bool:
        """
        Returns True if the build root is ready to be used, False otherwise.
        """
        assert self.build_root is not None, "Bug: the build root must be set"
        return (self.build_root() / self.MARKERFILE).exists()

    def build_root_mark_as_ready(self):
        """
        Mark the build root directory as ready
        """
        with open(self.build_root() / self.MARKERFILE, "w") as fd:
            fd.write("This is a fwupd build environment created with fub")
            fd.write("See fub --help for available commands.")

    @classmethod
    def populate(cls, builddir: Path | None = None) -> Self:
        """
        Populate the commonly used directories.
        """
        repo_root = cls.find_repo_root()
        if builddir is None:
            builddir = cls.find_most_recent_build_root()
            if builddir is None:
                builddir = repo_root / "builddir"

        return cls(
            _repository_root=repo_root,
            _build_root=builddir,
        )

    def repopulate(self, builddir: Path | None = None):
        # make this easy to call
        if builddir is None:
            return

        # we don't ever update the repository root
        self._build_root = builddir


# The directories singleton that contains all our paths.
#
# Use repopulate() to initialize with a new base dir given by commandline args.
directories = Directories.populate()
