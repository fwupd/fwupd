# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'init' subcommand — run all setup steps in sequence

import argparse
from pathlib import Path

from .helpers import (
    MARKERFILE,
    OS_PROFILES,
    detect_os,
    logger,
    pip_install_package,
    venv_path,
    msg,
)


def register(subparsers):
    """Register the 'init' subcommand with argparse."""
    parser = subparsers.add_parser(
        "init",
        help="initialize development environment (runs all setup steps)",
        description=(
            "Run all setup steps in order: deps, venv, hooks, vscode, git. "
            "This is usually the first command and only needs to be run once. "
            "Use --no-* flags to skip individual steps."
        ),
    )
    parser.add_argument(
        "init_directory", nargs="?", type=Path, help="The directory to initialize"
    )
    parser.add_argument(
        "--wipe", action="store_true", help="remove the existing directory"
    )
    parser.add_argument(
        "--os",
        dest="os_profile",
        choices=OS_PROFILES,
        default=None,
        help="OS profile to use (default:autodetect)",
    )
    parser.add_argument(
        "-y", "--yes", action="store_true", help="say yes to all prompts"
    )
    parser.add_argument("-n", "--no", action="store_true", help="say no to all prompts")
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
    parser.set_defaults(func=run)


def run(args):
    """Run all setup steps in sequence."""
    from . import setup_cmd

    # The directory can be specified either as a positional argument
    # (init /path) or via the top-level -C flag. The positional takes
    # precedence.
    if args.init_directory:
        args.directory = args.init_directory

    venv = venv_path(args)

    logger.info(f"Initializing in {venv}")

    if (venv / MARKERFILE).exists():
        if not args.wipe:
            msg(args, f"Development environment already initialized in {venv}.")
            msg(args, "")
            msg(args, "To rebuild, run: ")
            msg(args, "    [bold]# fub build[/bold]")
            msg(args, "")
            msg(args, "To enter the environment:")
            msg(args, "")
            msg(args, "    [bold]# source venv/bin/activate[/bold]")
            return 0
        else:
            import shutil

            shutil.rmtree(venv)

    # yes has a tristate: True (always yes), False (always no), None (ask interactively)
    yes = None
    if args.yes:
        yes = True
    elif args.no:
        yes = False

    os_profile = args.os_profile
    if os_profile is None:
        os_profile = detect_os()
        if not os_profile:
            pip_install_package(args, "distro")
            os_profile = detect_os()
        if os_profile:
            logger.info(f"Using OS profile: '{os_profile}'")

    if args.deps:
        msg(args, "● Installing dependencies")
        deps_args = argparse.Namespace(
            os_profile=os_profile,
            yes=yes,
            quiet=getattr(args, "quiet", False),
            debug=getattr(args, "debug", False),
        )
        rc = setup_cmd.run_deps(deps_args)
        if rc != 0:
            return rc
    else:
        logger.info("Skipping dependencies (--no-deps)")

    msg(args, "● Setting up virtual environment")
    venv_args = argparse.Namespace(
        directory=args.directory,
        quiet=getattr(args, "quiet", False),
        debug=getattr(args, "debug", False),
    )
    rc = setup_cmd.run_venv(venv_args)
    if rc != 0:
        return rc

    if args.pre_commit_hooks:
        msg(args, "● Setting up hooks")
        hooks_args = argparse.Namespace(
            pre_push_hooks=args.pre_push_hooks,
            quiet=getattr(args, "quiet", False),
            debug=getattr(args, "debug", False),
        )
        rc = setup_cmd.run_hooks(hooks_args)
        if rc != 0:
            return rc
    else:
        logger.info("Skipping hooks (--no-hooks)")

    if args.vscode:
        msg(args, "● Copying VS Code settings")
        vscode_args = argparse.Namespace(
            quiet=getattr(args, "quiet", False), debug=getattr(args, "debug", False)
        )
        rc = setup_cmd.run_vscode(vscode_args)
        if rc != 0:
            return rc
    else:
        logger.info("Skipping VS Code settings (--no-vscode)")

    msg(args, "● Configuring git")
    git_args = argparse.Namespace(
        quiet=getattr(args, "quiet", False), debug=getattr(args, "debug", False)
    )
    rc = setup_cmd.run_git(git_args)
    if rc != 0:
        return rc

    # Mark the venv directory
    (venv / MARKERFILE).touch(exist_ok=True)

    msg(args, "Initial setup complete.")
    msg(args, "")
    msg(args, "To build or rebuild fwupd within development environment run:")
    msg(args, "")
    msg(args, "    [bold]# fub build[/bold]")
    msg(args, "")
    msg(args, "See [bold]fub --help[/bold] for more commands.")
    msg(args, "")
    msg(args, "To enter fwupd development environment run:")
    msg(args, "")
    msg(args, "    [bold]# source venv/bin/activate[/bold]")
    msg(args, "")

    return 0
