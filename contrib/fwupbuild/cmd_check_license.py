# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
# Copyright 2026 Red Hat
#
# 'check-license' subcommand — check for license incompatibilities

import os
import re
from dataclasses import dataclass
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import logger, printer

REQUIRED_LICENSE = "LGPL-2.1-or-later"


@dataclass
class LicenseError:
    file: Path


@dataclass
class SpdxMissing(LicenseError):
    def __str__(self) -> str:
        return "missing an SPDX license identifier"


@dataclass
class SpdxDuplicated(LicenseError):
    def __str__(self) -> str:
        return "duplicate SPDX license identifiers"


@dataclass
class InvalidSpdxLicense(LicenseError):
    lic: str

    def __str__(self) -> str:
        return f"does not contain {REQUIRED_LICENSE} ({self.lic.strip()})"


@dataclass
class CopyrightMissing(LicenseError):
    def __str__(self) -> str:
        return "missing a Copyright line"


DISALLOWED_COPYRIGHT_SYMBOLS = ["(C)", "(c)", "©", "  "]  # last entry: double-space


@dataclass
class InvalidCopyrightSymbol(LicenseError):
    found_symbols: list[str]

    def __str__(self) -> str:
        return f"contains disallowed symbol(s) in the Copyright line: {' '.join(self.found_symbols)}"


def register(subparsers):
    """Register the 'check-license' subcommand."""

    parser = subparsers.add_parser(
        "check-license",
        help="Check the source files for correct license tags",
        description=(
            "Checks all files in git (or the given files) for the correct "
            "Copyright line and SPDX license tag."
        ),
    )
    parser.add_argument(
        "--suffixes",
        type=str,
        help="comma-separated list of suffixes to check, including dot",
        default=".c,.h,.rs,.py",
    )
    parser.add_argument(
        "--exclude-directories",
        type=str,
        help="comma-separated list of directories to exclude, relative to repo root",
        default="data,contrib",
    )
    parser.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="the file(s) to check. If set, --suffixes and --exclude-directories args are ignored",
    )

    parser.set_defaults(func=run)


def run(args):
    os.chdir(directories.repository_root())
    repo = GitRepo(directories.repository_root())

    if args.files:
        try:
            files_to_check: list[Path] = repo.resolve_files(args.files)
        except FileNotFoundError as e:
            printer.error(str(e))
            return 1
    else:
        suffixes = [s.strip() for s in args.suffixes.split(",")]
        excluded_directories = [d.strip() for d in args.exclude_directories.split(",")]

        files_to_check = repo.find_files(
            suffixes=suffixes,
            exclude_directories=excluded_directories,
        )

    # so we don't detect ourselves as having duplicate identifiers...
    spdx_license_identifier = "-".join(["SPDX", "License", "Identifier"])  # noqa: FLY002

    errors: list[LicenseError] = []

    for file in files_to_check:
        logger.debug(f"Checking {file}")
        with open(file, encoding="utf-8") as f:
            content = f.read()
        lines: list[str] = content.split("\n")
        if len(lines) < 2:
            continue

        lic: str | None = None
        copyrights: list[str] = []

        for line in lines:
            if f"{spdx_license_identifier}:" in line:
                if lic is not None:
                    errors.append(SpdxDuplicated(file))
                    continue
                lic = line.split(":", maxsplit=1)[1]
            if re.search(r"\bCopyright\b", line):
                copyrights.append(line.strip())

        if not lic:
            errors.append(SpdxMissing(file))
        elif REQUIRED_LICENSE not in lic:
            errors.append(InvalidSpdxLicense(file, lic))

        if not copyrights:
            errors.append(CopyrightMissing(file))

        for cp in copyrights:
            found = [s for s in DISALLOWED_COPYRIGHT_SYMBOLS if s in cp]
            if found:
                errors.append(InvalidCopyrightSymbol(file, found))

    for e in errors:
        printer.error(f"{e.file}: {e}")

    return 1 if errors else 0
