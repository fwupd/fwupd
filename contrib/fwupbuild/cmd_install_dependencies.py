# SPDX-License-Identifier: LGPL-2.1-or-later
#
# install-dependencies subcommand — list build dependencies


from .logger import printer
from .osprofile import OsArch, OsName, UnknownArchException, UnknownOsException


def register(subparsers):
    """Register the 'install-dependencies' subcommand."""

    parser = subparsers.add_parser(
        "install-dependencies",
        help="Install build dependencies",
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

    parser.set_defaults(func=run)


def run(args):
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
        try:
            args.filter = OsArch.detect().value
        except UnknownArchException:
            printer.error("Unknown arch, cannot continue")
            return 1

    from .cmd_setup import setup_system_deps

    setup_system_deps(args.os_name, args.filter)

    return 0
