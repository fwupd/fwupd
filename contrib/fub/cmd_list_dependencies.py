# SPDX-License-Identifier: LGPL-2.1-or-later
#
# list-dependencies subcommand — list build dependencies


from .cli import argparse_func_wrapper
from .dependencies import Dependencies
from .directories import directories
from .logger import printer
from .osprofile import OsArch, OsName, UnknownOsException


def register(subparsers):
    """Register the 'list-dependencies' subcommand."""

    parser = subparsers.add_parser(
        "list-dependencies",
        help="List build dependencies",
    )
    parser.add_argument(
        "--os",
        dest="os_name",
        choices=list(OsName),
        default=None,
        help="OS name to use (default: autodetect)",
    )
    parser.add_argument(
        "--filter",
        dest="filter",
        default=None,
        help="Filter to use (default: machine arch)",
    )
    parser.add_argument(
        "--separator",
        type=str,
        default="\n",
        help="Separator between package names (default: '\\n')",
    )

    # parser.add_argument("files", nargs="*", default=None, help="File(s) to check")
    parser.set_defaults(func=argparse_func_wrapper(run))


def run(args):
    repo_root = directories.repository_root()

    if not args.os_name:
        try:
            args.os_name = OsName.detect()
        except UnknownOsException:
            profiles = " ".join(f"'{p}'" for p in OsName)
            printer.error(
                f"Could not detect OS profile. Use --os to specify one of {profiles}."
            )
            return 1

    if not args.filter:
        args.filter = str(OsArch.detect())

    dependencies = Dependencies.load()
    deps = dependencies.find_packages(distro=args.os_name, filter=args.filter)
    print(args.separator.join([d.package_name for d in deps]))

    return 0
