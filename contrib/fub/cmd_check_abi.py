# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'check-abi' subcommand — check for ABI incompatibilities

import logging
import sys
import tempfile
from pathlib import Path

from .directories import directories
from .git import GitRepo
from .logger import logger, printer
from .meson import Meson
from .runcmd import RunCmd


def register(subparsers):
    """Register the 'check-abi' subcommand."""

    parser = subparsers.add_parser(
        "check-abi",
        help="Check the ABI for incompatibilities",
        description=("Check two git revisions for ABI incompatibilities"),
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        default=False,
        help="Keep the temporary directory on exit",
    )
    parser.add_argument(
        "--old",
        required=True,
        type=str,
        help="the previous revision, considered the reference",
    )
    parser.add_argument(
        "--new",
        required=True,
        type=str,
        help="the new revision, to compare to the reference",
    )

    parser.set_defaults(func=run)


def run(args):
    if args.old == args.new:
        return 0

    cmd = RunCmd(["abidiff", "--version"])
    if not cmd.success:
        printer.error("Failed to find or run abidiff")
        return 1

    repo = GitRepo(directories.repository_root())

    kwargs = {}
    # TemporaryDirectory(delete=) requires Python 3.12
    if sys.version_info >= (3, 12):
        kwargs["delete"] = args.keep is False

    with tempfile.TemporaryDirectory(prefix="fub-check-abi", **kwargs) as tmpdir_name:
        tmpdir = Path(tmpdir_name)
        clone = repo.clone_into(tmpdir)
        logger.debug(f"Working git repo in {clone.root}")

        old_ref = args.old
        new_ref = args.new
        old_sha = clone.as_sha(old_ref)
        new_sha = clone.as_sha(new_ref)
        printer.message(f"Comparing old: {old_sha} ({old_ref})")
        printer.message(f"       to new: {new_sha} ({new_ref})")

        prefix = Path("usr")

        for sha in [old_sha, new_sha]:
            logger.debug(f"Building and installing {sha} in {tmpdir}")
            with clone.checkout(sha):
                meson = Meson(
                    builddir=tmpdir / str(sha),
                    prefix=Path(f"/{prefix}"),
                    meson_args=[
                        "--libdir=lib",
                        "-Dauto_features=disabled",
                        "-Db_coverage=false",
                        "-Dtests=false",
                    ],
                    capture_logs=True,
                )
                meson.cwd = clone.root
                meson.capture_logs = True
                cmd = meson.setup()
                if not cmd.success:
                    cmd.log_stdout(level=logging.ERROR)
                    cmd.log_stderr(level=logging.ERROR)
                    printer.error(f"Failed meson setup for {sha}")
                    return 1
                cmd = meson.build()
                if not cmd.success:
                    cmd.log_stdout(level=logging.ERROR)
                    cmd.log_stderr(level=logging.ERROR)
                    printer.error(f"Failed meson build for {sha}")
                    return 1
                cmd = meson.install(destdir=tmpdir / str(sha))
                if not cmd.success:
                    printer.error(f"Failed meson install for {sha}")
                    return 1

        include_dir = prefix / "include"
        libfwupd_so = prefix / "lib" / "libfwupd.so"
        cmd = RunCmd(
            [
                "abidiff",
                "--headers-dir1",
                tmpdir / str(old_sha) / include_dir,
                "--headers-dir2",
                tmpdir / str(new_sha) / include_dir,
                "--drop-private-types",
                "--suppressions",
                str(clone.root / "contrib" / "ci" / "abidiff.suppr"),
                "--fail-no-debug-info",
                "--no-added-syms",
                tmpdir / str(old_sha) / libfwupd_so,
                tmpdir / str(new_sha) / libfwupd_so,
            ]
        )
        print(cmd.stdout)
        if not cmd.success:
            print(cmd.stderr)
            printer.error("ABI check failed, see above output for details")

        return cmd.returncode
