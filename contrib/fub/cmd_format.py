# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'format' subcommand — reformat C code to match project style

import os
import shutil
from pathlib import Path

from .directories import directories
from .git import InvalidGitRef, GitRepo
from .logger import logger, printer
from .runcmd import RunCmd

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
        help="reformat all changes since this commit",
    )
    parser.set_defaults(func=run)


def find_clang_formatter() -> str:
    """Find a usable clang-format-diff tool.

    Returns the formatter command string, raises a FileNotFoundError
    if none are found.
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

    raise FileNotFoundError


def run(args):
    """Reformat C code to match project style."""
    repo_root = directories.repository_root()

    if args.commit:
        base = args.commit
    elif (base := os.getenv("GITHUB_BASE_REF")) is not None:
        base = f"origin/{base}"
    else:
        base = "HEAD"

    sha: str

    try:
        repo = GitRepo.default()
        sha = repo.as_sha(base)
    except InvalidGitRef:
        logger.debug(f"git describe {base} failed, falling back to HEAD")
        sha = repo.current_sha

    logger.info(f"Reformatting code against {base} ({sha})")

    try:
        formatter = find_clang_formatter()
    except FileNotFoundError:
        printer.error(
            "No clang-format-diff tool found, install one of: "
            + ", ".join(f for f in CLANG_DIFF_FORMATTERS if "/" not in f),
        )
        return 1

    logger.debug(f"Using formatter: {formatter}")

    try:
        diff = repo.diff(sha, context_lines=0)
        if not diff:
            printer.message("No changes to reformat.")
            return 0
    except InvalidGitRef:
        printer.error(f"Failed to get git diff against {base} ({sha})")
        return 1

    fmt = RunCmd(
        [formatter, "-i", "-regex", r"^.*\.(c|h|proto)$", "-p1"],
        input=diff,
        cwd=repo_root,
    )
    if not fmt.success:
        printer.error("clang-format-diff failed")
        return 1

    return 0
