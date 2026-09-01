# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'format' subcommand — reformat C code to match project style

import os
import shutil
from pathlib import Path

from .helpers import (
    error,
    find_repo_root,
    logger,
    msg,
    RunCmd,
)

CLANG_DIFF_FORMATTERS = [
    "clang-format-diff-11",
    "clang-format-diff-13",
    "clang-format-diff",
    "/usr/share/clang/clang-format-diff.py",
]


def register(subparsers):
    """Register the 'format' subcommand."""
    parser = subparsers.add_parser(
        "format",
        help="reformat C code to match project style",
        description=("Reformat source code to match the project coding style. "),
    )
    parser.add_argument(
        "commit",
        nargs="?",
        default=None,
        help="reformat all changes since this commit (default: HEAD)",
    )
    parser.set_defaults(func=run)


def find_clang_formatter():
    """Find a usable clang-format-diff tool.

    Returns the formatter command string, or None if none found.
    """
    for formatter in CLANG_DIFF_FORMATTERS:
        if "/" in formatter:
            # Absolute path, check directly
            if not Path(formatter).is_file():
                continue
            result = RunCmd([formatter, "--help"])
            if result.success:
                return formatter
            continue
        if shutil.which(formatter):
            result = RunCmd([formatter, "--help"])
            if result.success:
                return formatter

    return None


def run(args):
    """Reformat C code to match project style."""
    root = find_repo_root()

    # Determine the base commit to diff against
    base = os.getenv("GITHUB_BASE_REF")
    if base:
        base = f"origin/{base}"
    elif args.commit:
        base = args.commit
    else:
        base = "HEAD"

    # Verify the base ref is valid
    result = RunCmd(["git", "describe", base], cwd=root)
    if not result.success:
        logger.debug(f"git describe {base} failed, falling back to HEAD")
        base = "HEAD"

    msg(args, f"Reformatting code against {base}")

    # Find a clang formatter
    formatter = find_clang_formatter()
    if formatter is None:
        error(args, "No clang-format-diff tool found.")
        error(
            args,
            "Install one of: "
            + ", ".join(f for f in CLANG_DIFF_FORMATTERS if "/" not in f),
        )
        return 1

    logger.info(f"Using formatter: {formatter}")

    # Get the diff
    diff = RunCmd(["git", "diff", "-U0", base], cwd=root)
    if not diff.success:
        error(args, f"Failed to get git diff against {base}")
        return 1

    if not diff.stdout.strip():
        msg(args, "No changes to reformat.")
        return 0

    # Run clang-format-diff on the diff
    fmt = RunCmd(
        [formatter, "-i", "-regex", r"^.*\.(c|h|proto)$", "-p1"],
        input=diff.stdout,
        cwd=root,
    )
    if not fmt.success:
        error(args, "clang-format-diff failed")
        return 1

    return 0
