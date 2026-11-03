# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
#
# 'check-headers' subcommand — check headers for correctness


import os
from dataclasses import dataclass
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import logger, printer


@dataclass
class IncludeError:
    file: Path


@dataclass
class PrivateHeaderInclude(IncludeError):
    private_header: str

    def __str__(self) -> str:
        return f"uses private header {self.private_header}"


@dataclass
class InternalHeaderInclude(IncludeError):
    internal_header: str

    def __str__(self) -> str:
        return f"use of internal header {self.internal_header}, use top-level includes only"


@dataclass
class BothToplevelIncludes(IncludeError):
    headers: list[str]

    def __str__(self) -> str:
        return f"contains both {', '.join(self.headers)}"


@dataclass
class ToplevelWithSublevelInclude(IncludeError):
    toplevel_header: str
    sublevel_header: str

    def __str__(self) -> str:
        return (
            f"contains {self.toplevel_header} but also includes {self.sublevel_header}"
        )


@dataclass
class MissingConfigH(IncludeError):
    def __str__(self) -> str:
        return "does not include config.h"


@dataclass
class SelfInclude(IncludeError):
    def __str__(self) -> str:
        return "includes itself"


@dataclass
class DuplicateIncludes(IncludeError):
    def __str__(self) -> str:
        return "contains duplicate includes"


@dataclass
class SuperfluousInclude(IncludeError):
    correct_header: str
    superflouous_header: str

    def __str__(self) -> str:
        return f"includes {self.superflouous_header} which is implied by {self.correct_header}"


@dataclass
class UnicodeError(IncludeError):
    def __str__(self) -> str:
        return "failed unicode parsing"


def register(subparsers):
    """Register the 'check-headers' subcommand."""

    parser = subparsers.add_parser(
        "check-headers",
        help="Check source headers for correctness",
    )

    parser.add_argument("files", nargs="*", default=None, help="File(s) to check")
    parser.set_defaults(func=run)


def get_includes(file: Path) -> list[str]:
    includes: list[str] = []
    with open(file) as f:
        for line in f.read().split("\n"):
            if line.find("#include") == -1:
                continue
            if line.find("waive-pre-commit") > 0:
                continue
            for char in ["<", ">", '"']:
                line = line.replace(char, "")
            for char in ["\t"]:
                line = line.replace(char, " ")
            includes.append(line.split(" ")[-1])
    return sorted(includes)


def pathify(paths: list[str]) -> list[Path]:
    return [Path(p) for p in paths]


def run(args):
    os.chdir(directories.repository_root())
    repo = GitRepo(directories.repository_root())

    libfwupd_public_headers = pathify(["libfwupd/fwupd.h"])
    libfwupd_headers = repo.find_files(
        suffixes=[".h"],
        include_directories=["libfwupd"],
        file_filter=lambda f: f not in libfwupd_public_headers,
    )

    libfwupdplugin_public_headers = pathify(["libfwupdplugin/fwupdplugin.h"])
    libfwupdplugin_headers = repo.find_files(
        suffixes=[".h"],
        include_directories=["libfwupdplugin"],
        file_filter=lambda f: f not in libfwupdplugin_public_headers,
    )

    toplevel_headers = libfwupd_public_headers + libfwupdplugin_public_headers
    toplevel_headers_filenames_only = [Path(f).name for f in toplevel_headers]

    internal_headers = libfwupd_headers + libfwupdplugin_headers
    internal_headers_filenames_only = [Path(f).name for f in internal_headers]

    ignore_files = pathify(
        [
            "libfwupd/fwupd-context-test.c",
            "libfwupd/fwupd-thread-test.c",
            "libfwupdplugin/fu-fuzzer-main.c",
        ]
    )

    ignore_dirs = pathify(
        [
            "contrib",
        ]
    )

    if args.files:
        try:
            resolved = repo.resolve_files(args.files)
        except FileNotFoundError as e:
            printer.error(str(e))
            return 1
        files_to_check = [
            f
            for f in resolved
            if f not in ignore_files
            and not any(f.is_relative_to(d) for d in ignore_dirs)
        ]
    else:
        files_to_check = repo.find_files(
            suffixes=[".c", ".h"],
            include_directories=["libfwupd", "libfwupdplugin", "plugins", "src"],
            file_filter=lambda f: f not in ignore_files,
        )

    errors: list[IncludeError] = []
    for file in files_to_check:
        logger.debug(f"Checking {file}")

        try:
            includes = get_includes(file)
        except UnicodeDecodeError:
            errors.append(UnicodeError(file))
            continue

        if (
            file.is_relative_to("plugins")
            and not file.name.endswith("-test.c")
            and not file.name.endswith("tool.c")
        ):
            for include in includes:
                if include.endswith("private.h"):
                    errors.append(PrivateHeaderInclude(file, private_header=include))
                    continue

                if include in internal_headers + internal_headers_filenames_only:
                    errors.append(InternalHeaderInclude(file, internal_header=include))

        for toplevel_header in toplevel_headers:
            toplevel_includes = get_includes(toplevel_header)
            toplevel_includes_nopath = [Path(f).name for f in toplevel_includes]

            # we do not need both toplevel headers
            if set(toplevel_headers_filenames_only).issubset(set(includes)):
                errors.append(
                    BothToplevelIncludes(
                        file,
                        headers=toplevel_headers_filenames_only,
                    )
                )

            # toplevel not listed
            if Path(toplevel_header).name not in includes:
                continue

            # includes toplevel and *also* something listed in the toplevel
            for include in includes:
                if include in toplevel_includes or include in toplevel_includes_nopath:
                    errors.append(
                        ToplevelWithSublevelInclude(
                            file,
                            toplevel_header=toplevel_header,
                            sublevel_header=include,
                        )
                    )

        # check for missing config.h
        if file.suffix == ".c" and "config.h" not in includes:
            errors.append(MissingConfigH(file))

        # check for headers including themselves
        if file.suffix == ".h" and file.name in includes:
            errors.append(SelfInclude(file))

        # check for duplicate includes
        if sorted(set(includes)) != includes:
            errors.append(DuplicateIncludes(file))

        # check for one header implying the other
        implied_headers = {
            "fu-common.h": ["xmlb.h"],
            "fwupdplugin.h": [
                "gio/gio.h",
                "glib.h",
                "glib-object.h",
                "xmlb.h",
                "fwupd.h",
            ]
            + libfwupd_headers,
            "gio/gio.h": ["glib.h", "glib-object.h"],
            "glib-object.h": ["glib.h"],
            "xmlb.h": ["gio/gio.h"],
        }
        for key, values in implied_headers.items():
            for value in values:
                if key in includes and value in includes:
                    errors.append(
                        SuperfluousInclude(
                            file, correct_header=key, superflouous_header=value
                        )
                    )

    for e in errors:
        printer.error(f"{e.file}: {e}")

    return 1 if errors else 0
