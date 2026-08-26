# SPDX-License-Identifier: LGPL-2.1-or-later

import argparse
import logging
from pathlib import Path

from .helpers import ColorFormatter


def main(argv=None):
    """Main entry point for fub."""
    parser = argparse.ArgumentParser(
        prog="fub",
        description="fwupd developer helper tool",
    )
    parser.add_argument(
        "-v", "--verbose", action="count", default=0, help="increase debug output"
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        default=False,
        help="silence all non-error output",
    )
    parser.add_argument(
        "-C",
        dest="directory",
        type=Path,
        default=None,
        help="use the given directory as the venv directory",
    )

    subparsers = parser.add_subparsers(dest="command", help="available commands")

    # Import and register all command modules
    from . import (
        check_abi_cmd,
        init_cmd,
        setup_cmd,
        build_cmd,
        test_cmd,
        run_cmd,
        format_cmd,
        docker_cmd,
    )

    init_cmd.register(subparsers)
    setup_cmd.register(subparsers)
    build_cmd.register(subparsers)
    test_cmd.register(subparsers)
    run_cmd.register(subparsers)
    format_cmd.register(subparsers)
    docker_cmd.register(subparsers)
    check_abi_cmd.register(subparsers)

    args = parser.parse_args(argv)

    match args.verbose:
        case 0:
            level = logging.WARNING
        case 1:
            level = logging.INFO
        case _:
            level = logging.DEBUG
    logging.basicConfig(level=level)
    logging.getLogger().handlers[0].setFormatter(ColorFormatter())
    logging.getLogger("fub").setLevel(level)

    if args.command is None or not hasattr(args, "func"):
        parser.print_help()
        return 1

    try:
        return args.func(args)
    except KeyboardInterrupt:
        return 130
