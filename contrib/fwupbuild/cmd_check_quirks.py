# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2026 Red Hat
#
# 'check-quirks' subcommand — check quirk files for correctness

import os
import re
from dataclasses import dataclass
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import printer


@dataclass
class QuirkError:
    file: Path
    line: str


@dataclass
class WhitespaceError(QuirkError):
    def __str__(self) -> str:
        return "has leading or trailing whitespace"


@dataclass
class SectionHeaderError(QuirkError):
    def __str__(self) -> str:
        return "has invalid section header"


@dataclass
class LineError(QuirkError):
    def __str__(self) -> str:
        return "has invalid line"


@dataclass
class GTypeError(QuirkError):
    def __str__(self) -> str:
        return "has invalid GType"


@dataclass
class SpacingError(QuirkError):
    def __str__(self) -> str:
        return "has invalid spacing"


def register(subparsers):
    """Register the 'check-quirks' subcommand."""

    parser = subparsers.add_parser(
        "check-quirks",
        help="Check quirks for correctness",
    )

    parser.add_argument("files", nargs="*", type=Path, help="File(s) to check")
    parser.set_defaults(func=run)


GTYPE_PREFIXES = [
    "G_DEFINE_TYPE(",
    "G_DEFINE_TYPE_WITH_PRIVATE(",
    "G_DEFINE_TYPE_WITH_CODE(",
]


def gtype_is_valid(gtype: str) -> bool:
    """Return True if gtype is a concrete device GType (not Plugin/Firmware/Backend/Image)."""
    return bool(gtype) and not any(
        gtype.endswith(token) for token in ["Plugin", "Firmware", "Backend", "Image"]
    )


def find_gtypes(source_files: list[Path]) -> list[str]:
    """Return all valid GType names found in the given C source files."""
    gtypes: list[str] = []
    for file in source_files:
        with open(file, encoding="utf-8") as f:
            for line in f.read().split("\n"):
                for prefix in GTYPE_PREFIXES:
                    if line.startswith(prefix):
                        gtype = line[len(prefix) :].split(",")[0]
                        if gtype_is_valid(gtype):
                            gtypes.append(gtype)
                        break
    return gtypes


def run(args):
    os.chdir(directories.repository_root())

    repo = GitRepo(directories.repository_root())
    gtype_files = repo.find_files(
        suffixes=[".c"],
        include_directories=["libfwupdplugin"],
        file_filter=lambda f: bool(re.match(r"fu-.*-device\.c", f.name)),
    )

    gtype_files += repo.find_files(
        suffixes=[".c"],
        include_directories=["plugins"],
        file_filter=lambda f: bool(re.match(r"fu-.*\.c", f.name)),
    )

    gtypes = find_gtypes(gtype_files)
    if not gtypes:
        printer.error("No GType definitions found; cannot validate GType quirk values")
        return 1

    if args.files:
        try:
            files_to_check = repo.resolve_files(args.files)
        except FileNotFoundError as e:
            printer.error(str(e))
            return 1
    else:
        files_to_check = repo.find_files(
            suffixes=[".quirk"],
            include_directories=["data", "plugins"],
        )

    errors: list[QuirkError] = []
    for file in files_to_check:
        with open(file, encoding="utf-8") as f:
            for line in f.read().split("\n"):
                if line.startswith(" ") or line.endswith(" "):
                    errors.append(WhitespaceError(file, line))
                    continue
                if not line or line.startswith("#"):
                    continue
                if line.startswith("["):
                    if not line.endswith("]"):
                        errors.append(SectionHeaderError(file, line))
                        continue
                else:
                    sections = line.split(" = ")
                    if len(sections) != 2:
                        errors.append(LineError(file, line))
                        continue
                    if sections[0] == "GType" and sections[1] not in gtypes:
                        errors.append(GTypeError(file, line))
                        continue
                    for section in sections:
                        if section.strip() != section:
                            errors.append(SpacingError(file, line))
                            break

    for e in errors:
        printer.error(f"{e.file}: {e}: '{e.line}'")

    return 1 if errors else 0
