# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Shared utilities for fwupbuild

from collections.abc import Callable
from contextlib import contextmanager
from dataclasses import dataclass
from functools import cached_property
from pathlib import Path
from typing import Self

from .directories import directories
from .logger import printer
from .runcmd import RunCmd


@dataclass(frozen=True)
class GitSha:
    """
    Represents a single git sha in a given repository.
    """

    sha: str
    _repo: "GitRepo"

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, GitSha):
            return False
        return self.sha == other.sha and self._repo.root == other._repo.root

    def __str__(self) -> str:
        return self.sha

    def __hash__(self) -> int:
        return hash(self.sha + str(self._repo.root))


@dataclass
class GitRepo:
    root: Path

    @classmethod
    def default(cls) -> Self:
        """
        The default git repository based on our directory lookup paths
        """
        return cls(directories.repository_root())

    @property
    def current_ref(self) -> str | None:
        """
        Return the current named ref for HEAD or None if no named ref is available.
        """
        cmd = RunCmd(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=self.root, check=True
        )
        ref = cmd.stdout.strip()
        if ref == "HEAD":
            return None
        return ref

    @property
    def current_sha(self) -> GitSha:
        """
        Return the sha for HEAD
        """
        cmd = RunCmd(["git", "rev-parse", "HEAD"], cwd=self.root, check=True)
        return GitSha(sha=cmd.stdout.strip(), _repo=self)

    def as_sha(self, ref: str) -> GitSha:
        """
        Return the sha for the given named ref
        """
        cmd = RunCmd(["git", "rev-parse", ref], cwd=self.root, check=True)
        return GitSha(sha=cmd.stdout.strip(), _repo=self)

    def clone_into(self, destdir: Path, depth: int = 0) -> Self:
        """
        Clones the git repo into the given target directory. The resulting
        repo is destdir/<reponame>
        """

        git_args = []
        if depth > 0:
            git_args.append(f"--depth={depth}")

        destdir.mkdir(exist_ok=True, parents=True)
        RunCmd(["git", "clone", str(self.root)] + git_args, cwd=destdir, check=True)
        return GitRepo(root=destdir / self.root.name)

    @cached_property
    def files(self) -> list[Path]:
        """
        Returns the list of files in this git repo as paths relative
        to the repository root.
        """
        cmd = RunCmd(["git", "ls-files", "-z"], cwd=self.root, check=True)
        return [Path(p) for p in cmd.stdout.split("\0") if p]

    def find_files(
        self,
        *,
        suffixes: list[str] | None = None,
        exclude_directories: list[Path | str] | None = None,
        include_directories: list[Path | str] | None = None,
        file_filter: Callable[[Path], bool] | None = None,
        as_absolute_paths: bool = False,
    ) -> list[Path]:
        """
        Find files in this git repo that:
        - have one of the provided suffixes
        - is NOT in one of the exclude directories
        - IS in the include directories
        - returns True for the given file_filter function. The filter function is
          called with the relative Path of each file

        Returned paths are relative to the repository root directory,
        unless as_absolute_paths is True.
        """

        root = self.root.resolve()
        includes = [Path(d) for d in (include_directories or [])]
        excludes = [Path(d) for d in (exclude_directories or [])]

        def has_suffix(f: Path) -> bool:
            return not suffixes or f.suffix in suffixes

        def in_directory(path: Path, directory: Path) -> bool:
            if not directory.is_absolute():
                directory = root / directory
            return path.is_relative_to(directory)

        def ignore_path(f: Path) -> bool:
            path = (root / f).resolve()

            for d in excludes:
                if in_directory(path, d):
                    return True

            if includes:
                for d in includes:
                    if in_directory(path, d):
                        return False
                return True
            return False

        def filter_allows_file(f: Path) -> bool:
            return file_filter is None or file_filter(f)

        def use_file(f: Path) -> bool:
            return has_suffix(f) and not ignore_path(f) and filter_allows_file(f)

        files = [f for f in self.files if use_file(f)]
        if not as_absolute_paths:
            return files

        result = []
        for f in files:
            abs_f = (self.root / f).resolve()
            if not abs_f.is_relative_to(root):
                printer.warning(f"{f} resolves outside repository root, skipping")
                continue
            result.append(abs_f)
        return result

    def resolve_files(
        self, files: list[Path | str], *, as_absolute_paths: bool = False
    ) -> list[Path]:
        """
        Convert the given list of file names or paths into
        a list of paths relative to the repository root.

        Raises a FileNotFoundError if a file is outside the repository root.
        Prints a warning for any file that is not tracked by git.

        If as_absolute_paths is True the returned paths are absolute paths instead.
        """
        resolved: list[Path] = []
        root = self.root.resolve()
        tracked = set(self.files)
        for f in [Path(f) for f in files]:
            if not f.is_absolute():
                f = root / f
            f = f.resolve()
            if not f.is_relative_to(root):
                raise FileNotFoundError(f"{f} is not inside {root}")
            if not f.exists():
                raise FileNotFoundError(f"{f} does not exist")
            relative = f.relative_to(root)
            if relative not in tracked:
                printer.warning(f"{relative} is not tracked by git")
            resolved.append(f if as_absolute_paths else relative)
        return resolved

    @contextmanager
    def checkout(self, sha: GitSha):
        """
        Check out the given sha and restore state of the repo on context
        manager exit.
        """
        current_ref = self.current_ref or self.current_sha
        try:
            RunCmd(["git", "checkout", sha.sha], cwd=self.root, check=True)
            # invalidate cache
            self.__dict__.pop("files", None)
            assert self.current_sha == sha
            yield
        finally:
            RunCmd(["git", "checkout", str(current_ref)], cwd=self.root, check=True)
            self.__dict__.pop("files", None)
