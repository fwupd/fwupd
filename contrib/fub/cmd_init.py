# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'init' subcommand — run all setup steps in sequence

import argparse
import os
import shutil
from pathlib import Path

from .cli import argparse_func_wrapper
from .directories import directories
from .logger import logger, printer
from .osprofile import OsName, UnknownOsException


def detect_shell():
    """Detect the user's login shell.

    Returns a tuple of (shell_name, activate_command, activate_path) where:
    - shell_name: 'fish', 'zsh', 'bash', 'sh', or 'unknown'
    - activate_command: the command to source the activate script
    - activate_path: path to the activate script
    """
    shell_path = os.environ.get("SHELL", "")
    shell_name = os.path.basename(shell_path) if shell_path else "unknown"

    build_root = directories.build_root()
    activate_path = build_root / "bin" / "activate"
    activate_fish_path = build_root / "bin" / "activate.fish"

    if shell_name == "fish" and activate_fish_path.exists():
        return ("fish", "source", str(activate_fish_path))
    elif shell_name in ("zsh", "bash") and activate_path.exists():
        return (shell_name, "source", str(activate_path))
    elif activate_path.exists():
        return ("sh", ".", str(activate_path))
    elif activate_fish_path.exists():
        return ("fish", "source", str(activate_fish_path))

    return (shell_name, "source", str(activate_path))


def print_blurb():
    shell_name, activate_cmd, activate_path = detect_shell()
    printer.message("")
    printer.message("To enter the fwupd development environment environment:")
    printer.message("")
    printer.message(f"    [bold]# {activate_cmd} {activate_path}[/bold]")
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
    parser.add_argument(
        "build_root",
        type=Path,
        default=None,
        nargs="?",
        help="The directory to initialize",
    )
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

    if args.build_root is None and args.directory is None:
        args.build_root = Path(
            printer.ask(
                "Specify directory to use for the build environment (default: 'builddir/')",
                default_answer="builddir",
            )
        )

    # The directory can be specified either as a positional argument
    # (init /path) or via the top-level -C flag.
    if args.directory and args.directory != args.build_root:
        printer.error(f"'-C {args.directory}' differs from '{args.build_root}'")
        return 1

    args.directory = args.build_root
    directories.repopulate(args.directory)
    build_root = directories.build_root()
    logger.info(f"Initializing in {build_root}")

    # We only wipe something that we recognize as our own build root
    if build_root.exists():
        if args.wipe:
            if not directories.build_root_is_initialized or any(
                (build_root / fn).exists() for fn in [".git", "meson.build"]
            ):
                printer.error(
                    f"Refusing to wipe '{build_root}', it does not look like a fwupd build environment"
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
                    f"Directory '{build_root}' already exists but does not look like a fwupd build environment"
                )
                return 1

            printer.message(
                f"Development environment already initialized in '{build_root}'."
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
        printer.message("● Installing system dependencies")
        if (rc := cmd_setup.setup_system_deps(osname)) != 0:
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
