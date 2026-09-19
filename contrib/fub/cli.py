# SPDX-License-Identifier: LGPL-2.1-or-later

import argparse
import functools
import importlib
import logging
import os
import pkgutil
from pathlib import Path

from .directories import directories
from .logger import ColorFormatter, Yes, logger, printer


def setup_globals(args: argparse.Namespace) -> None:
    printer.quiet = args.quiet
    printer.default_yn_answer = Yes(args)
    directories.repopulate(args.directory)


def argparse_func_wrapper(func):
    """
    Used from CLI invocations of the respective parser setup
    # parser.set_defaults(func=func_wrapper(run))
    """

    @functools.wraps(func)
    def wrapper(args):
        setup_globals(args)
        return func(args)

    return wrapper


def main(argv: list[str] | None = None) -> int:
    """Main entry point for fub."""
    parser = argparse.ArgumentParser(
        prog="fub",
        description="""
The fwupd developer helper tool.

This command provides utilities to build and test fwupd as well as utilities to
check and maintain the source.

This command should not be used for building distribution packages. Use normal
meson build commands instead.
""",
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
        help="change into the directory to perform the command",
    )
    parser.add_argument(
        "-y",
        "--yes",
        default=True if os.environ.get("CI") else None,
        action="store_const",
        const=True,
        help="say yes to all prompts",
    )
    parser.add_argument(
        "-n",
        "--no",
        dest="yes",
        action="store_const",
        const=False,
        help="say no to all prompts",
    )

    subparsers = parser.add_subparsers(dest="command", help="available commands")

    prefix = "cmd_"
    pkg_dir = str(Path(__file__).parent)
    for _, module_name, _ in pkgutil.iter_modules([pkg_dir]):
        if module_name.startswith(prefix):
            try:
                module = importlib.import_module(f".{module_name}", package=__package__)
                globals()[module_name] = module

                # Each command must be in cmd_foo.py and provide a register()
                # function that takes the subparsers and sets it up as follows:
                #
                # parser = subparsers.add_parser("foo", ...)
                # ... set up the parser for the foo command here...
                # parser.set_defaults(func=run)
                #
                # def run(args):
                #    ... function to be invoked for this subcommand ...

                module.register(subparsers)
            except Exception as e:
                logger.error(f"Failed to import module {module_name}: {e}")

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
