# SPDX-License-Identifier: LGPL-2.1-or-later
#
# 'check-abi' subcommand — check for ABI incompatibilities

import tempfile
from pathlib import Path

from .helpers import (
    error,
    logger,
    msg,
    GitRepo,
    Meson,
    RunCmd,
)


def register(subparsers):
    """Register the 'check-abi' subcommand."""

    parser = subparsers.add_parser(
        "check-abi",
        help="Check the ABI for incompatibilities",
        description=(
            "Generate a Dockerfile from a CI template for the given "
            "distro/version/arch, and optionally build the container image."
        ),
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

    repo = GitRepo.default()
    sha = repo.current_sha

    with tempfile.TemporaryDirectory(prefix="fub-check-abi") as tmpdir_name:
        tmpdir = Path(tmpdir_name)
        clone = repo.clone_into(tmpdir)
        logger.debug(f"Working git repo in {clone.root}")

        old_ref = args.old
        new_ref = args.new
        old_sha = clone.as_sha(old_ref)
        new_sha = clone.as_sha(new_ref)
        msg(args, f"Comparing old: {old_sha} ({old_ref})")
        msg(args, f"       to new: {new_sha} ({new_ref})")

        prefix = Path("usr")

        for sha in [old_sha, new_sha]:
            logger.debug(f"Building and installing {sha} in {tmpdir}")
            with clone.checkout(sha):
                meson = Meson(
                    builddir=tmpdir / sha,
                    prefix=Path(f"/{prefix}"),
                    meson_args=[
                        "--libdir=lib",
                        "-Dauto_features=disabled",
                        "-Db_coverage=false",
                        "-Dtests=false",
                    ],
                    capture_logs=True,
                )
                meson.setup()
                meson.build()
                meson.install(destdir=tmpdir / sha)

        include_dir = prefix / "include"
        libfwupd_so = prefix / "lib" / "libfwupd.so"
        cmd = RunCmd(
            [
                "abidiff",
                "--headers-dir1",
                tmpdir / old_sha / include_dir,
                "--headers-dir2",
                tmpdir / new_sha / include_dir,
                "--drop-private-types",
                "--suppressions",
                "contrib/ci/abidiff.suppr",
                "--fail-no-debug-info",
                "--no-added-syms",
                tmpdir / old_sha / libfwupd_so,
                tmpdir / new_sha / libfwupd_so,
            ]
        )
        if not cmd.success:
            error(args, "ABI check failed, see above output for details")
        return cmd.returncode
