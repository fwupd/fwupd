#!/usr/bin/env python3
# Copyright 2026 Red Hat
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# A wrapper around cargo to be used with meson. Cargo and meson are peculiar about where to
# put things and how to build them, this script provides a bridge between these two
# utilities.
#
# Usage: meson-cargo.py [OPTIONS] build|doc|test [OPTIONS] [CARGO_OPTIONS]
#
# Use in meson.build as:
#     # these can also be passed as commandline options
#     cargo_env = environment()
#     cargo_env.set('MESON_BUILDTYPE', get_option('buildtype'))
#     cargo_env.set('MESON_BUILD_ROOT', meson.project_build_root())
#     cargo_env.set('WORKSPACE_CARGO_TOML', meson.current_source_dir() / "Cargo.toml")
#     cargo_wrapper = find_program('meson-cargo.py')
#
#     # Build a crate (and use the stamp file)
#     mything = custom_target(
#       'mything',
#       output: 'mything.so',
#       command: [
#         cargo_wrapper,
#         '-p', 'my-crate',
#         'build',
#         '--stamp', '@OUTPUT@',
#       ],
#       env: cargo_env,
#       console: true,
#       install: false,
#       build_by_default: true,
#
#       depends: [...],
#       depend_files: [
#         files(
#           'Cargo.toml',
#           'Cargo.lock',
#           'my-crate/Cargo.toml',
#           'my-crate/src/lib.rs',
#           ...
#         ),
#       ],
#     )
#
#     # For cargo doc builds
#     custom_target(
#       'rust-doc',
#       output: 'doc',
#       command: [cargo_wrapper, 'doc', '--output', '@OUTPUT@'],
#       env: cargo_env,
#       console: true,
#       install: false,
#       build_by_default: true,
#
#       depends: [...],
#     )
#
#     # For cargo test of a single crate
#     test(
#       'blah-cratename',
#       cargo_wrapper,
#       args: ['-p', 'cratename', 'test', '--nocapture'],
#       env: cargo_env,
#       timeout: 120,
#       suite: 'rust',
#
#       depends: [...],
#     )

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def die(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def relative_path(value) -> Path:
    if Path(value).is_absolute():
        raise argparse.ArgumentTypeError(f"{value!r} must be a relative path")
    return value


def main():
    parser = argparse.ArgumentParser(
        prog="meson-cargo.py",
        description="Build a Rust crate via cargo from within a meson build.",
    )
    parser.add_argument(
        "-p",
        "--package",
        dest="crate",
        metavar="CRATE",
        help="Run cargo only for the given crate",
    )
    parser.add_argument(
        "--meson-build-root",
        dest="meson_build_root",
        metavar="DIR",
        default=os.environ.get("MESON_BUILD_ROOT"),
        help="Meson build root (default: $MESON_BUILD_ROOT)",
    )
    parser.add_argument(
        "--meson-buildtype",
        dest="meson_buildtype",
        metavar="TYPE",
        default=os.environ.get("MESON_BUILDTYPE"),
        help="Meson build type (default: $MESON_BUILDTYPE)",
    )
    parser.add_argument(
        "--meson-rust-target",
        dest="meson_rust_target",
        metavar="TRIPLE",
        default=os.environ.get("MESON_RUST_TARGET"),
        help="Rust target triple for cross-compilation (default: $MESON_RUST_TARGET)",
    )
    parser.add_argument(
        "--cargo-bin",
        dest="cargo_bin",
        metavar="PATH",
        default=os.environ.get("CARGO", shutil.which("cargo")),
        help="Path to the cargo binary (default: $CARGO or cargo from PATH)",
    )
    parser.add_argument(
        "--workspace-cargo-toml",
        dest="workspace_cargo_toml",
        metavar="FILE",
        default=os.environ.get("WORKSPACE_CARGO_TOML", "Cargo.toml"),
        help="Path to the workspace Cargo.toml (default: $WORKSPACE_CARGO_TOML or Cargo.toml)",
    )

    subparsers = parser.add_subparsers(dest="subcommand", required=True)

    build_sp = subparsers.add_parser("build")
    build_out = build_sp.add_mutually_exclusive_group(required=True)
    build_out.add_argument(
        "--output",
        metavar="FILE",
        type=relative_path,
        help="Copy cargo artifact to FILE (relative to $MESON_BUILD_ROOT)",
    )
    build_out.add_argument(
        "--stamp",
        metavar="FILE",
        type=relative_path,
        help="Touch FILE as a stamp file",
    )
    build_sp.add_argument(
        "--copy",
        dest="copydir",
        metavar="DIR",
        type=relative_path,
        help="Also copy the artifact into DIR (relative to $MESON_BUILD_ROOT)",
    )
    build_sp.add_argument("cargo_args", nargs="*")

    doc_sp = subparsers.add_parser("doc")
    doc_sp.add_argument(
        "--copy",
        dest="copydir",
        metavar="DIR",
        type=relative_path,
        help="Copy OUTPUT into DIR (relative to $MESON_BUILD_ROOT)",
    )
    doc_sp.add_argument("--output", metavar="PATH", default="doc")
    doc_sp.add_argument("cargo_args", nargs="*")

    test_sp = subparsers.add_parser("test")
    test_sp.add_argument("cargo_args", nargs="*")

    args, unknown_args = parser.parse_known_args()

    if not args.meson_build_root:
        parser.error("--meson-build-root or $MESON_BUILD_ROOT must be set")
    meson_build_root = Path(args.meson_build_root)

    meson_buildtype = args.meson_buildtype or ""
    match meson_buildtype:
        case "release" | "plain":
            cargo_profile = "release"
            cargo_profile_dir = "release"
        case _:
            cargo_profile = "dev"
            cargo_profile_dir = "debug"

    cargo = args.cargo_bin
    if not cargo:
        die("cargo not found")
    if (
        subprocess.run(
            [cargo, "--version"], capture_output=True, check=False
        ).returncode
        != 0
    ):
        die(f"cargo binary '{cargo}' failed to run")

    cargo_target_dir = Path(
        os.environ.get("CARGO_TARGET_DIR") or meson_build_root / "rust-target"
    )

    meson_rust_target = args.meson_rust_target or ""
    cargo_target_args = []
    cargo_target_subdir = ""
    if meson_rust_target:
        cargo_target_args = ["--target", meson_rust_target]
        cargo_target_subdir = meson_rust_target

    meson_build_root_abs = meson_build_root.resolve()
    copydir = getattr(args, "copydir", None)

    if copydir is not None and not (
        meson_build_root_abs / copydir
    ).resolve().is_relative_to(meson_build_root_abs):
        die("--copy DIR not inside --meson-build-root ")

    output = getattr(args, "stamp", None) or getattr(args, "output", None)
    cargo_extra_args = []

    if args.subcommand == "build":
        if (
            not (meson_build_root_abs / output)
            .resolve()
            .is_relative_to(meson_build_root_abs)
        ):
            die("output path not inside $MESON_BUILD_ROOT")
    elif args.subcommand == "doc":
        cargo_profile_dir = ""

    crate_args = ["-p", args.crate] if args.crate else []
    cargo_args = getattr(args, "cargo_args", [])

    env = os.environ.copy()
    env.pop("CFLAGS", None)

    uninstalled = meson_build_root / "meson-uninstalled"
    pkg_config_path = env.get("PKG_CONFIG_PATH", "")
    env["PKG_CONFIG_PATH"] = (
        f"{uninstalled}:{pkg_config_path}" if pkg_config_path else str(uninstalled)
    )

    workspace_cargo_toml = args.workspace_cargo_toml

    # cargo test uses [profile.test] by default; passing --profile overrides that
    # and activates [profile.dev] (panic=abort), breaking the test harness's catch_unwind.
    profile_args = [f"--profile={cargo_profile}"] if args.subcommand != "test" else []

    cmd = [
        cargo,
        args.subcommand,
        f"--manifest-path={workspace_cargo_toml}",
        f"--target-dir={cargo_target_dir}",
        *profile_args,
        *cargo_target_args,
        *crate_args,
        *cargo_extra_args,
        *cargo_args,
        *unknown_args,
    ]
    result = subprocess.run(cmd, env=env, check=False)
    if result.returncode != 0:
        sys.exit(result.returncode)

    if args.subcommand == "test":
        return

    if (stampfile := getattr(args, "stamp", None)) is not None:
        (meson_build_root / stampfile).touch()
        return

    src = cargo_target_dir
    if cargo_target_subdir:
        src = src / cargo_target_subdir
    if cargo_profile_dir:
        src = src / cargo_profile_dir
    src = src / Path(output).name

    dest = meson_build_root / output
    if src.is_dir():
        shutil.rmtree(dest, ignore_errors=True)
        shutil.copytree(src, dest, symlinks=True)
    else:
        shutil.copy2(src, dest)

    if copydir is not None:
        copydir_full = meson_build_root / copydir
        copydir_full.mkdir(parents=True, exist_ok=True)
        dest_in_copydir = copydir_full / src.name
        if src.is_dir():
            if dest_in_copydir.exists():
                shutil.rmtree(dest_in_copydir)
            shutil.copytree(src, dest_in_copydir, symlinks=True)
        else:
            shutil.copy2(src, dest_in_copydir)


if __name__ == "__main__":
    main()
