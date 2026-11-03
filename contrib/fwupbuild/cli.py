# SPDX-License-Identifier: LGPL-2.1-or-later

import argparse
import importlib
import inspect
import logging
import os
import pkgutil
from pathlib import Path

from .directories import directories
from .logger import ColorFormatter, Yes, logger, printer
from .osprofile import Shell


def setup_globals(args: argparse.Namespace) -> None:
    printer.quiet = args.quiet
    printer.default_yn_answer = Yes.from_args(args)
    directories.repopulate(args.directory)


def main(argv: list[str] | None = None) -> int:
    """Main entry point for fwupbuild."""
    parser = argparse.ArgumentParser(
        prog="fwupbuild",
        description="""
The fwupd developer helper tool.

This command provides utilities to build and test fwupd as well as utilities to
check and maintain the source.

This command should not be used for building distribution packages. Use normal
meson build commands instead.
""",
    )

    parser.add_argument(
        "--print-completion",
        choices=list(Shell),
        help=argparse.SUPPRESS,
        type=str,
        default=None,
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
                #
                # Or if the parser should take any arguments not handled
                # by argparse.
                #
                # def run(args, remaining: list[str]):
                #    ... function to be invoked for this subcommand ...
                #

                module.register(subparsers)
            except Exception as e:  # noqa: BLE001
                logger.error(f"Failed to import module {module_name}: {e}")

    args, remaining = parser.parse_known_args(argv)
    if args.print_completion:
        try:
            import shtab  # pylint: disable=import-outside-toplevel

            print(shtab.complete(parser, shell=args.print_completion))
        except NotImplementedError:
            logger.info(f"shtab does not support {args.print_completion}, skipping")
            return 0
        except ModuleNotFoundError:
            logger.error("shtab is required for completion generation")
            return 1
        return 0

    match args.verbose:
        case 0:
            level = logging.WARNING
        case 1:
            level = logging.INFO
        case _:
            level = logging.DEBUG
    logging.basicConfig(level=level)
    logging.getLogger().handlers[0].setFormatter(ColorFormatter())
    logging.getLogger("fwupbuild").setLevel(level)

    if args.command is None or not hasattr(args, "func"):
        parser.print_help()
        return 1

    # Unpack known-args/remaining args and pass them as first + second
    # argument into whatever our run func is
    run_func = args.func
    run_func_args: dict
    match list(inspect.signature(run_func).parameters):
        case [first, second, *_]:
            run_func_args = {first: args, second: remaining}
        case [first]:
            if remaining:
                parser.error(f"unrecognized arguments: {' '.join(remaining)}")
            run_func_args = {first: args}
        case []:
            run_func_args = {}

    try:
        setup_globals(args)
        return run_func(**run_func_args)
    except KeyboardInterrupt:
        return 130
