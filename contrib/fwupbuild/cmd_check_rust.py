# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 Red Hat
#
# 'check-rust' subcommand — check that Rust files for correctness

import os
from dataclasses import dataclass
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import printer

# Vendored meson wrap dependencies don't get checked
EXCLUDED_DIRECTORIES: list[Path] = [Path("subprojects")]


@dataclass
class RustError:
    file: Path


@dataclass
class UntrackedFileError(RustError):
    def __str__(self) -> str:
        return "is not tracked in git"


def register(subparsers):
    """Register the 'check-rust' subcommand."""

    parser = subparsers.add_parser(
        "check-rust",
        help="Check Rust files for correctness",
    )
    parser.set_defaults(func=run)


def run(args):
    repo_root = directories.repository_root()
    os.chdir(repo_root)
    repo = GitRepo(repo_root)

    untracked = [
        f
        for f in repo.untracked_files
        if f.suffix == ".rs"
        and not any(excluded in f.parents for excluded in EXCLUDED_DIRECTORIES)
    ]

    errors = [UntrackedFileError(f) for f in untracked]

    for e in errors:
        printer.error(f"{e.file}: {e}")

    return 1 if errors else 0
