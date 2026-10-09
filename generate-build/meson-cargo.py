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
        help="Touch FILE as a stamp file, ignore --output if given",
    )
    build_sp.add_argument("cargo_args", nargs="*")

    doc_sp = subparsers.add_parser("doc")
    doc_sp.add_argument(
        "--copy",
        dest="copydir",
        metavar="DIR",
        type=relative_path,
        help="Copy documentation output into DIR (relative to $MESON_BUILD_ROOT)",
    )
    doc_sp.add_argument("cargo_args", nargs="*")

    test_sp = subparsers.add_parser("test")
    test_sp.add_argument("cargo_args", nargs="*")

    args, unknown_args = parser.parse_known_args()

    if not args.meson_build_root:
        parser.error("--meson-build-root or $MESON_BUILD_ROOT must be set")
    meson_build_root = Path(args.meson_build_root)

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

    meson_buildtype = args.meson_buildtype or ""
    match meson_buildtype:
        case "release" | "plain":
            cargo_profile = "release"
            cargo_profile_dir = "release"
        case _:
            cargo_profile = "dev"
            cargo_profile_dir = "debug"

    match args.subcommand:
        # cargo test uses [profile.test] by default; passing --profile overrides that
        # and activates [profile.dev] (panic=abort), breaking the test harness's catch_unwind.
        case "test":
            cargo_profile = None
        # cargo doc always builds into target/doc
        case "doc":
            cargo_profile_dir = "doc"

    cargo_target_dir = Path(
        os.environ.get("CARGO_TARGET_DIR") or meson_build_root / "rust-target"
    )

    # When cross-compiling, pass the Rust target triple to cargo.
    # Cargo then places artifacts under target/<triple>/<profile>/ instead
    # of target/<profile>/, so we track that extra path component separately.
    meson_rust_target = args.meson_rust_target or ""
    cargo_target_args = []
    cargo_target_subdir = ""
    if meson_rust_target:
        cargo_target_args = ["--target", meson_rust_target]
        cargo_target_subdir = meson_rust_target

    output_src_base = cargo_target_dir
    if cargo_target_subdir:
        output_src_base = output_src_base / cargo_target_subdir
    if cargo_profile_dir:
        output_src_base = output_src_base / cargo_profile_dir
    match args.subcommand:
        case "build":
            # Meson's @OUTPUT@ which we likely get passed includes the full path
            # but cargo doesn't honor that. So our --output arg may be
            # src/something.so but cargo compiles this into debug/something.so.
            output_dest = args.stamp or args.output
            output_src = output_src_base / Path(output_dest).name
        case "doc":
            output_src = output_src_base
            output_dest = args.copydir
        case _:
            output_src = None
            output_dest = None

    meson_build_root_abs = meson_build_root.resolve()
    if output_dest is not None and not (
        meson_build_root_abs / output_dest
    ).resolve().is_relative_to(meson_build_root_abs):
        die(f"{output_dest} must be inside $MESON_BUILD_ROOT")

    crate_args = ["-p", args.crate] if args.crate else []
    cargo_args = getattr(args, "cargo_args", [])

    # Unset the CFLAGS from the environment because they mess with our -sys crates
    # ABI tests
    env = os.environ.copy()
    env.pop("CFLAGS", None)

    # We need to pick up our own *-uninstalled.pc files
    uninstalled = meson_build_root / "meson-uninstalled"
    pkg_config_path = env.get("PKG_CONFIG_PATH", "")
    env["PKG_CONFIG_PATH"] = (
        f"{uninstalled}:{pkg_config_path}" if pkg_config_path else str(uninstalled)
    )

    workspace_cargo_toml = args.workspace_cargo_toml

    profile_args = [f"--profile={cargo_profile}"] if cargo_profile else []

    cmd = [
        cargo,
        args.subcommand,
        f"--manifest-path={workspace_cargo_toml}",
        f"--target-dir={cargo_target_dir}",
        *profile_args,
        *cargo_target_args,
        *crate_args,
        *cargo_args,
        *unknown_args,
    ]
    print(f"Running {' '.join(cmd)}")
    result = subprocess.run(cmd, env=env, check=False)
    if result.returncode != 0:
        sys.exit(result.returncode)

    if args.subcommand == "test":
        return

    if (stampfile := getattr(args, "stamp", None)) is not None:
        (meson_build_root / stampfile).touch()
        return

    if output_src is None or output_dest is None:
        return

    dest = meson_build_root / output_dest
    if output_src.is_dir():
        shutil.rmtree(dest, ignore_errors=True)
        shutil.copytree(output_src, dest, symlinks=True)
    else:
        shutil.copy2(output_src, dest)


if __name__ == "__main__":
    main()
