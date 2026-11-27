# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'download-test-firmware' subcommand — download the test firmware

import shutil
import tempfile
from pathlib import Path

from .git import GitRepo
from .logger import logger


def register(subparsers):
    """Register the 'download-test-firmware' subcommand."""
    parser = subparsers.add_parser(
        "download-test-firmware",
        help="download the test firmware repo",
        description=(
            "Download the test firmware repo and copy it into the directory "
            "structure at the --dest directory. The destination directory layout "
            "depends on the --tests argument."
        ),
    )
    parser.add_argument(
        "--dest",
        type=Path,
        help="Destination directory to copy the test firmware into",
        required=True,
    )
    parser.add_argument(
        "--tests",
        choices=["ci-tests", "installed-tests"],
        required=True,
        help="The type of tests to copy",
    )
    parser.set_defaults(func=run)


def run(args):
    """Download the test firmware and install it in the given paths"""

    with tempfile.TemporaryDirectory() as d:
        repo = GitRepo.clone("https://github.com/fwupd/fwupd-test-firmware", cwd=d)

        files = repo.root / args.tests
        logger.debug(f"Copying {files} to {args.dest}")
        shutil.copytree(
            files,
            args.dest,
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns(".*"),
        )

    return 0
