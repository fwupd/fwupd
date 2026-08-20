# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'init' subcommand — run all setup steps in sequence

import argparse
import shutil
from pathlib import Path

from .cli import argparse_func_wrapper
from .directories import directories
from .logger import logger, printer
from .osprofile import OsName, UnknownOsException


def print_blurb():
    printer.message("")
    printer.message("To build, run: ")
    printer.message("    [bold]# fub build[/bold]")
    printer.message("")
    printer.message("To run the test suite, run: ")
    printer.message("    [bold]# fub test[/bold]")
    printer.message("")
    printer.message("To specify a specific build root, use:")
    printer.message(f"     [bold]# fub -C {directories.build_root()} build[/bold]")
    printer.message(f"     [bold]# fub -C {directories.build_root()} test[/bold]")
    printer.message("")
    printer.message(
        "See [bold]fub --help[/bold] for more information and other commands."
    )
    printer.message("")
    printer.message("To enter the fwupd development environment environment:")
    printer.message("")
    printer.message(
        f"    [bold]# source {directories.build_root()}/bin/activate[/bold]"
    )


def register(subparsers):
    """Register the 'init' subcommand with argparse."""
    parser = subparsers.add_parser(
        "init",
        help="initialize the development environment from scratch",
        description=(
            "This is usually the first command and only needs to be run once. "
            "Use --no-* flags to skip individual steps. See the setup "
            "command for details on the various steps."
        ),
    )
    parser.add_argument("builddir", type=Path, help="The directory to initialize")
    parser.add_argument(
        "--wipe", action="store_true", help="remove the existing directory"
    )
    parser.add_argument(
        "--os",
        dest="osname",
        choices=OsName,
        default=None,
        help="OS profile to use (default:autodetect)",
    )
    parser.add_argument(
        "--deps",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="whether to install system dependencies (default: yes)",
    )
    parser.add_argument(
        "--pre-commit-hooks",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="whether to install a pre-commit hook (default: yes)",
    )
    parser.add_argument(
        "--vscode",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Whether to install VS Code settings (default: yes)",
    )
    parser.add_argument(
        "--pre-push-hooks",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="whether to install pre-push test hooks (default: no)",
    )
    parser.set_defaults(func=argparse_func_wrapper(run))


def run(args):
    """Initialize a new build root directory, run all setup steps in sequence."""
    from . import cmd_setup

    # The directory can be specified either as a positional argument
    # (init /path) or via the top-level -C flag.
    if args.directory and args.directory != args.builddir:
        printer.error(f"'-C {args.directory}' differs from '{args.builddir}'")
        return 1

    args.directory = args.builddir
    directories.repopulate(args.directory)
    build_root = directories.build_root()
    logger.info(f"Initializing in {build_root}")

    # We only wipe something that we recognize as our own build root
    if build_root.exists():
        if args.wipe:
            if not directories.build_root_is_initialized:
                printer.error(
                    f"Refusing to wipe {build_root}, it does not look like a fwupd build environment"
                )
                return 1

            try:
                shutil.rmtree(directories.build_root())
            except PermissionError as e:
                logger.error(e)
                return 1
        else:
            if not directories.build_root_is_initialized:
                printer.error(
                    f"Directory {build_root} already exists but does not look like a fwupd build environment"
                )
                return 1

            printer.message(
                f"Development environment already initialized in {build_root}."
            )
            print_blurb()
            return 0

    try:
        if args.osname:
            osname = OsName.from_string(args.osname)
        else:
            osname = OsName.detect()
    except UnknownOsException:
        osnames = " ".join(f"'{p}'" for p in OsName)
        printer.error(
            f"Could not detect OS profile. Use --os to specify one of {osnames}."
        )
        return 1

    logger.info(f"Using OS profile: '{osname}'")

    if args.deps:
        printer.message("● Installing dependencies")
        if (rc := cmd_setup.setup_deps(osname)) != 0:
            return rc
    else:
        logger.info("Skipping dependencies (--no-deps)")

    printer.message("● Setting up virtual environment")
    if (rc := cmd_setup.setup_venv()) != 0:
        return rc

    if args.pre_commit_hooks:
        printer.message("● Setting up hooks")
        if (rc := cmd_setup.setup_hooks(args.pre_push_hooks)) != 0:
            return rc
    else:
        logger.info("Skipping hooks (--no-pre-commit-hooks)")

    if args.vscode:
        printer.message("● Copying VS Code settings")
        if (rc := cmd_setup.setup_vscode()) != 0:
            return rc
    else:
        logger.info("Skipping VS Code settings (--no-vscode)")

    printer.message("● Configuring git")
    if (rc := cmd_setup.setup_git()) != 0:
        return rc

    # Mark the venv directory
    directories.build_root_mark_as_ready()

    printer.message("Initial setup complete.")
    print_blurb()

    return 0
