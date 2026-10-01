# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
#
# 'check-headers' subcommand — check headers for correctness


from dataclasses import dataclass
from pathlib import Path

from .cli import argparse_func_wrapper
from .directories import directories
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
    parser.set_defaults(func=argparse_func_wrapper(run))


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


def run(args):
    repo_root = directories.repository_root()

    def find_headers(dir: Path, exclude: list[str]) -> list[str]:
        headers = [str(h.relative_to(repo_root)) for h in repo_root.glob(f"{dir}/*.h")]
        return [h for h in headers if h not in exclude]

    libfwupd_public_headers = ["libfwupd/fwupd.h"]
    libfwupd_headers = find_headers("libfwupd", exclude=libfwupd_public_headers)

    libfwupdplugin_public_headers = ["libfwupdplugin/fwupdplugin.h"]
    libfwupdplugin_headers = find_headers(
        "libfwupdplugin", exclude=libfwupdplugin_public_headers
    )

    toplevel_headers = libfwupd_public_headers + libfwupdplugin_public_headers
    toplevel_headers_filenames_only = [Path(f).name for f in toplevel_headers]

    internal_headers = libfwupd_headers + libfwupdplugin_headers
    internal_headers_filenames_only = [Path(f).name for f in internal_headers]

    ignore_files = [
        repo_root / "libfwupd/fwupd-context-test.c",
        repo_root / "libfwupd/fwupd-thread-test.c",
        repo_root / "libfwupdplugin/fu-fuzzer-main.c",
    ]

    ignore_dirs = [
        repo_root / "contrib",
    ]

    if args.files:
        files_to_check: list[Path] = [repo_root / f for f in args.files]
    else:
        globs = [
            repo_root.glob("libfwupd/*.[c|h]"),
            repo_root.glob("libfwupdplugin/*.[c|h]"),
            repo_root.glob("plugins/*/*.[c|h]"),
            repo_root.glob("src/*.[c|h]"),
        ]
        files_to_check: list[Path] = [f for glob in globs for f in glob]

    errors = []
    for file in filter(
        lambda f: f not in ignore_files
        and not any(f.is_relative_to(d) for d in ignore_dirs),
        files_to_check,
    ):
        logger.debug(f"Checking {file}")

        try:
            includes = get_includes(file)
        except UnicodeDecodeError:
            errors.append(UnicodeError(file))
            continue

        if (
            file.is_relative_to(repo_root / "plugins")
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
            toplevel_includes = get_includes(repo_root / toplevel_header)
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
        fn = e.file.relative_to(repo_root)
        printer.error(f"{fn}: {e}")

    return 1 if errors else 0
