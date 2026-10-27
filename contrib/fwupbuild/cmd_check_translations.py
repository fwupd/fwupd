# SPDX-License-Identifier: LGPL-2.1-or-later
#
#
# 'check-translations' subcommand — check translation files


import tempfile
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import printer
from .runcmd import RunCmd


def register(subparsers):
    """Register the 'check-translations' subcommand."""

    parser = subparsers.add_parser(
        "check-translations",
        help="Check translations for missing files",
    )

    parser.set_defaults(func=run)


def run(args):
    repo = GitRepo(directories.repository_root())
    with tempfile.TemporaryDirectory() as d:
        dest = Path(d) / "fwupbuild-check-translations"
        repo.clone_into(dest)

        podir = dest / "fwupd" / "po"
        RunCmd(["intltool-update", "-m"], cwd=podir, check=True)
        try:
            with open(podir / "missing") as missing:
                for line in missing.read().splitlines():
                    printer.error(f"Missing from POTFILES.in or POTFILES.skip: {line}")
            return 1
        except FileNotFoundError:
            return 0
