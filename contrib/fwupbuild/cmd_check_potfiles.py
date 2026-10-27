# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2022 Richard Hughes <richard@hughsie.com>
# Copyright 2026 Red Hat
#
# 'check-potfiles' subcommand — check for missing entries in po/POTFILES.in

import os
from dataclasses import dataclass
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import printer

POTFILES_IN: Path = Path("po") / "POTFILES.in"
POTFILES_SKIP: Path = Path("po") / "POTFILES.skip"
TOKENS = ['_("', "TRANSLATORS"]


@dataclass
class PotfileError:
    file: Path


@dataclass
class DuplicateEntries(PotfileError):
    def __str__(self) -> str:
        return "contains duplicate entries"


@dataclass
class MissingFromPotfiles(PotfileError):
    def __str__(self) -> str:
        return f"is missing from {POTFILES_IN}"


@dataclass
class ConflictingPotfiles(PotfileError):
    def __str__(self) -> str:
        return f"is in both {POTFILES_IN} and {POTFILES_SKIP}"


def register(subparsers):
    """Register the 'check-potfiles' subcommand."""

    parser = subparsers.add_parser(
        "check-potfiles",
        help="Check for missing translated files from po/POTFILES.in",
        description=(
            "Checks source files for translatable strings and verifies "
            "they are listed in po/POTFILES.in."
        ),
    )
    parser.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="the file(s) to check. If set, default file discovery is skipped",
    )

    parser.set_defaults(func=run)


def run(args):
    repo_root = directories.repository_root()
    os.chdir(repo_root)
    repo = GitRepo(repo_root)
    errors: list[PotfileError] = []

    potfiles_path = POTFILES_IN
    try:
        with open(potfiles_path, encoding="utf-8") as f:
            entries: list[str] = sorted(
                line
                for line in f.read().split("\n")
                if line and not line.startswith("#")
            )
            potfiles = set(entries)
            if len(potfiles) != len(entries):
                errors.append(DuplicateEntries(POTFILES_IN))

    except FileNotFoundError:
        printer.error(f"{potfiles_path}: file not found")
        return 1

    try:
        with open(POTFILES_SKIP, encoding="utf-8") as f:
            entries: list[str] = sorted(
                line
                for line in f.read().split("\n")
                if line and not line.startswith("#")
            )
            potfiles_skip = set(entries)
            if len(potfiles_skip) != len(entries):
                errors.append(DuplicateEntries(POTFILES_SKIP))
    except FileNotFoundError:
        potfiles_skip = set()

    for file in potfiles:
        if file in potfiles_skip:
            errors.append(ConflictingPotfiles(file))

    if args.files:
        try:
            files_to_check: list[Path] = repo.resolve_files(args.files)
        except FileNotFoundError as e:
            printer.error(str(e))
            return 1
    else:
        files_to_check = (
            repo.find_files(
                suffixes=[".c", ".py"], include_directories=["src", "plugins"]
            )
            + repo.find_files(suffixes=[".in"], include_directories=["policy"])
            + repo.find_files(suffixes=[".xml"], include_directories=["data"])
            + repo.find_files(
                suffixes=[".txt"],
                include_directories=["libfwupdplugin/tests/bios-attrs"],
            )
        )

    for file in files_to_check:
        if str(file) not in potfiles and str(file) not in potfiles_skip:
            with open(file, encoding="utf-8") as f:
                content = f.read()

            if any(token in content for token in TOKENS):
                errors.append(MissingFromPotfiles(file))

    for e in errors:
        printer.error(f"{e.file}: {e}")

    return 1 if errors else 0
