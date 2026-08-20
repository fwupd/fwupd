# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Shared utilities for fub

from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Self

from .directories import directories
from .runcmd import RunCmd


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
    def current_sha(self) -> str:
        """
        Return the sha for HEAD
        """
        cmd = RunCmd(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=self.root, check=True
        )
        sha = cmd.stdout.strip()
        if sha == "HEAD":
            cmd = RunCmd(["git", "rev-parse", "HEAD"], cwd=self.root, check=True)
            sha = cmd.stdout.strip()
        return sha

    def as_sha(self, ref: str) -> str:
        """
        Return the sha for the given named ref
        """
        cmd = RunCmd(["git", "rev-parse", ref], cwd=self.root, check=True)
        return cmd.stdout.strip()

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
    def checkout(self, sha: str):
        """
        Check out the given sha and restore state of the repo on context
        manager exit.
        """
        current_sha = self.current_sha
        try:
            RunCmd(["git", "checkout", sha], cwd=self.root, check=True)
            assert self.current_sha == sha
            yield
        finally:
            RunCmd(["git", "checkout", current_sha], cwd=self.root, check=True)
