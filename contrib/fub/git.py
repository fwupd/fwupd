# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Shared utilities for fub

from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Self

from .directories import directories
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

    @contextmanager
    def checkout(self, sha: GitSha):
        """
        Check out the given sha and restore state of the repo on context
        manager exit.
        """
        current_ref = self.current_ref or self.current_sha
        try:
            RunCmd(["git", "checkout", sha.sha], cwd=self.root, check=True)
            assert self.current_sha == sha
            yield
        finally:
            RunCmd(["git", "checkout", str(current_ref)], cwd=self.root, check=True)
