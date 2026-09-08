# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'check-headers' subcommand — check headers for correctness


from dataclasses import dataclass
from pathlib import Path

from .helpers import (
    error,
    find_repo_root,
    logger,
)


def register(subparsers):
    """Register the 'check-headers' subcommand."""

    parser = subparsers.add_parser(
        "check-headers",
        help="Check source headers for correctness",
    )

    parser.add_argument(
        "-C",
        dest="root",
        metavar="directory",
        required=False,
        default=find_repo_root(),
        type=Path,
        help="change into the given directory",
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


@dataclass
class IncludeError:
    file: Path
    msg: str


def run(args):
    root = args.root

    libfwupd_public_headers = ["libfwupd/fwupd.h"]
    libfwupd_headers = [
        str(h.relative_to(root))
        for h in root.glob("libfwupd/*.h")
        if h not in libfwupd_public_headers
    ]

    libfwupdplugin_public_headers = ["libfwupdplugin/fwupdplugin.h"]
    libfwupdplugin_headers = [
        str(h.relative_to(root))
        for h in root.glob("libfwupdplugin/*.h")
        if h not in libfwupdplugin_public_headers
    ]

    toplevel_headers = ["libfwupd/fwupd.h", "libfwupdplugin/fwupdplugin.h"]
    toplevel_headers_filenames_only = [Path(f).name for f in toplevel_headers]

    internal_headers = libfwupd_headers + libfwupdplugin_headers
    internal_headers_filenames_only = [Path(f).name for f in internal_headers]

    ignore_files = [
        root / "libfwupd/fwupd-context-test.c",
        root / "libfwupd/fwupd-thread-test.c",
        root / "libfwupdplugin/fu-fuzzer-main.c",
    ]

    status = 0

    if args.files:
        files_to_check: list[Path] = [root / f for f in args.files]
    else:
        globs = [
            root.glob("libfwupd/*.[c|h]"),
            root.glob("libfwupdplugin/*.[c|h]"),
            root.glob("plugins/*/*.[c|h]"),
            root.glob("src/*.[c|h]"),
        ]
        files_to_check: list[Path] = [f for glob in globs for f in glob]

    errors = []
    for file in files_to_check:
        if file in ignore_files:
            continue

        logger.debug(f"Checking {file}")

        includes = get_includes(file)

        if (
            file.is_relative_to(root / "plugins")
            and not file.name.endswith("self-test.c")
            and not file.name.endswith("tool.c")
        ):
            for include in includes:
                if include.endswith("private.h"):
                    errors.append(
                        IncludeError(file, f"use of private header {include}")
                    )
                    continue

                if include in internal_headers + internal_headers_filenames_only:
                    errors.append(
                        IncludeError(
                            file,
                            f"use of internal header {include}, use top-level includes only",
                        )
                    )

        for toplevel_header in toplevel_headers:
            toplevel_includes = get_includes(Path(toplevel_header))
            toplevel_includes_nopath = [Path(f).name for f in toplevel_includes]

            # we do not need both toplevel headers
            if set(toplevel_headers_filenames_only).issubset(set(includes)):
                errors.append(
                    IncludeError(
                        file,
                        f"contains both {', '.join(toplevel_headers_filenames_only)}",
                    )
                )

            # toplevel not listed
            if toplevel_header not in includes:
                continue

            # includes toplevel and *also* something listed in the toplevel
            for include in includes:
                if include in toplevel_includes or include in toplevel_includes_nopath:
                    errors.append(
                        IncludeError(
                            file,
                            (f"contains {toplevel_header} but also includes {include}"),
                        )
                    )

        # check for missing config.h
        if file.suffix == ".c" and "config.h" not in includes:
            errors.append(IncludeError(file, f"does not include config.h"))

        # check for headers including themselves
        if file.suffix == ".h" and file.name in includes:
            errors.append(IncludeError(file, "includes itself"))

        # check for duplicate includes
        if sorted(set(includes)) != includes:
            errors.append(IncludeError(file, f"contains duplicate includes"))

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
                        IncludeError(
                            file, f"contains {value} which is implied by {key}"
                        )
                    )

    if errors:
        status = 1

    for e in errors:
        error(args, f"{e.file}: {e.msg}")

    return status
