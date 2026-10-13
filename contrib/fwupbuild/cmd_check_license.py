# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
# Copyright 2026 Red Hat
#
# 'check-license' subcommand — check for license incompatibilities

import re
from dataclasses import dataclass
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import logger, printer


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
class CopyrightMissing(LicenseError):
    def __str__(self) -> str:
        return "missing a Copyright line"


@dataclass
class InvalidCopyrightSymbol(LicenseError):
    disallowed_symbols: list[str]

    def __str__(self) -> str:
        return f"must not contain any of {' '.join(self.disallowed_symbols)} in the Copyright line"


def register(subparsers):
    """Register the 'check-license' subcommand."""

    parser = subparsers.add_parser(
        "check-license",
        help="Check the source files for correct license tags",
        description=(
            "Checks all files in git (or the given files) for the correct, "
            "Copyright line and SDPX license tag."
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
    repo_root = directories.repository_root()
    rc = 0

    if args.files:
        files_to_check: list[Path] = [repo_root / f for f in args.files]
    else:
        repo = GitRepo(directories.repository_root())

        suffixes = [s.strip() for s in args.suffixes.split(",")]
        excluded_directories = [d.strip() for d in args.exclude_directories.split(",")]

        def include_file(f):
            if not f.suffix in suffixes:
                return False

            return not any(f.is_relative_to(d) for d in excluded_directories)

        files_to_check = [f for f in repo.files if include_file(f)]

    # so we don't detect ourselves as having duplicate identifiers...
    spdx_license_identifier = "-".join(["SPDX", "License", "Identifier"])  # noqa: FLY002

    errors: list[LicenseError] = []

    for file in files_to_check:
        logger.debug(f"Checking {file}")
        with open(file) as f:
            lines: list[str] = f.read().split("\n")
            if len(lines) < 2:
                continue

        lic: str | None = None
        copyrights: list[str] = []

        for line in lines:
            if line.find(f"{spdx_license_identifier}:") != -1:
                if lic is not None:
                    errors.append(SpdxDuplicated(file))
                    rc = 1
                lic = line.split(":")[1]
            if re.search(r"\bCopyright\b", line):
                copyrights.append(line.strip())

        if lic is None:
            errors.append(SpdxMissing(file))
            rc = 1

        if not copyrights:
            errors.append(CopyrightMissing(file))
            rc = 1

        for cp in copyrights:
            disallowed_symbols = ["(C)", "(c)", "©", "  "]
            if any(symbol in cp for symbol in disallowed_symbols):
                errors.append(InvalidCopyrightSymbol(file, disallowed_symbols))
                rc = 1

    for e in errors:
        printer.error(f"{e.file}: {e}")

    return rc
