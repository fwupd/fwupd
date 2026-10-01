#!/usr/bin/env bash
# Copyright 2026 Red Hat
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Usage: meson-cargo.sh [-p <crate>] [--copy DIR] [--stamp] <build|test|doc> [OUTPUT]
#
# Build the given crate (or $PWD) with cargo and copy the
# resulting files to OUTPUT (which must be a relative path within
# $MESON_BUILD_ROOT).
#
# Arguments:
#    -p         ... Run cargo only for the given crate
#    --copy DIR ... Copy OUTPUT to the DIR directory, relative to $MESON_BUILD_ROOT.
#    --stamp    ... Instead of copying a cargo artifact, touch OUTPUT as a stamp file.
#
# The --copy argument can work around meson expecting files in certain directories when we need them
# in others. OUTPUT is left in the real directory so meson is happy with it.
#
# The --stamp argument helps with forcing the build of a crate not (yet) used by anything else.

# Use in meson.build as:
#     cargo_env = environment()
#     cargo_env.set('MESON_BUILDTYPE', get_option('buildtype'))
#     cargo_env.set('MESON_BUILD_ROOT', meson.project_build_root())
#     cargo_env.set('WORKSPACE_CARGO_TOML', meson.current_source_dir() / "Cargo.toml")
#     cargo_wrapper = find_program('meson-cargo.sh')
#
#     # Build a crate (and use the stamp file)
#     mything = custom_target(
#       'mything',
#       output: 'mything.so',
#       command: [
#         cargo_wrapper,
#         '-p', 'my-crate',
#         '--stamp',
#         'build',
#         '@OUTPUT@',
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
#       command: [cargo_wrapper, 'doc', '@OUTPUT@'],
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
#       args: ['-p', 'cratename', 'test'],
#       env: cargo_env,
#       timeout: 120,
#       suite: 'rust',
#
#       depends: [...],
#     )

set -e

usage() {
    # Prints anything from the first line that is '#\n' to the first empty line
    sed -n '/^#$/ { :a; n; /^$/q; s/^#[ ]\?//p; ba }' "${BASH_SOURCE[0]}"
}

# Portable equivalent of GNU realpath -m: normalize path without requiring existence.
abs_path() {
    python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$1"
}

die() {
    echo "$1" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
    --help | -h)
        usage
        exit 0
        ;;
    --copy)
        COPYDIR="$2"
        if [[ -z "${COPYDIR}" ]]; then
            usage
            exit 1
        fi
        [[ "${COPYDIR}" != /* ]] || die "\$COPYDIR must be relative to \$MESON_BUILD_ROOT"
        shift 2
        ;;
    --stamp)
        STAMP=1
        shift
        ;;
    -p)
        if [[ -z "$2" ]]; then
            usage
            exit 1
        fi
        CRATE_ARGS=(-p "$2")
        shift 2
        ;;
    --*)
        echo "Unknown commandline argument $1"
        exit 1
        ;;
    *)
        break
        ;;
    esac
done

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

CARGO_SUBCOMMAND="$1"
case "${CARGO_SUBCOMMAND}" in
build | doc | test) ;;
*)
    die "Unsupported cargo subcommand '${CARGO_SUBCOMMAND}'"
    ;;
esac
shift

# Expected meson variables
if [[ -z "${MESON_BUILD_ROOT}" ]]; then
    die "\$MESON_BUILD_ROOT not set" >&2
fi
case "$MESON_BUILDTYPE" in
release | plain)
    CARGO_PROFILE=release
    CARGO_PROFILE_DIR=release/
    ;;
*)
    CARGO_PROFILE=dev
    CARGO_PROFILE_DIR=debug/
    ;;
esac

# Semi-expected cargo variables
CARGO="${CARGO:-$(command -v cargo)}"
if [[ -z "${CARGO_TARGET_DIR}" ]]; then
    CARGO_TARGET_DIR="${MESON_BUILD_ROOT}/rust-target"
fi

# When cross-compiling, pass the Rust target triple to cargo.
# Cargo then places artifacts under target/<triple>/<profile>/ instead
# of target/<profile>/, so we track that extra path component separately.
CARGO_TARGET_ARGS=()
CARGO_TARGET_SUBDIR=""
if [[ -n "${MESON_RUST_TARGET}" ]]; then
    CARGO_TARGET_ARGS=(--target "${MESON_RUST_TARGET}")
    CARGO_TARGET_SUBDIR="${MESON_RUST_TARGET}/"
fi

MESON_BUILD_ROOT_ABS=$(abs_path "${MESON_BUILD_ROOT}")
if [[ -n "${COPYDIR}" ]]; then
    COPYDIR_ABS=$(abs_path "${MESON_BUILD_ROOT}/${COPYDIR}")
    [[ "${COPYDIR_ABS}" == "${MESON_BUILD_ROOT_ABS}/"* ]] || die "\$COPYDIR not inside \$MESON_BUILD_ROOT"
fi

CARGO_EXTRA_ARGS=()
case "${CARGO_SUBCOMMAND}" in
build)
    OUTPUT="$1"
    shift
    [[ -n "${OUTPUT}" ]] || die "Missing \$OUTPUT argument"
    # It's meson-cargo.sh -p crate build, not the other way round
    [[ "${OUTPUT}" != -* ]] || die "\$OUTPUT looks like a commandline option"
    [[ "${OUTPUT}" != /* ]] || die "\$OUTPUT must be relative to \$MESON_BUILD_ROOT"
    OUTPUT_ABS=$(abs_path "${MESON_BUILD_ROOT}/${OUTPUT}")
    [[ "${OUTPUT_ABS}" == "${MESON_BUILD_ROOT_ABS}/"* ]] || die "\$OUTPUT not inside \$MESON_BUILD_ROOT"
    ;;
doc)
    # cargo doc always builds into target/doc so let's hack around that
    OUTPUT="${1:-doc}"
    CARGO_PROFILE_DIR=""
    CARGO_EXTRA_ARGS=(--no-deps)
    shift
    ;;
*) ;;
esac

# Unset the CFLAGS from the environment because they mess with our -sys crates
# ABI tests
unset CFLAGS

# We need to pick up our own *-uninstalled.pc files
export PKG_CONFIG_PATH="${MESON_BUILD_ROOT}/meson-uninstalled:${PKG_CONFIG_PATH}"

"${CARGO}" "${CARGO_SUBCOMMAND}" \
    --manifest-path="${WORKSPACE_CARGO_TOML:-Cargo.toml}" \
    --target-dir="${CARGO_TARGET_DIR}" \
    --profile="${CARGO_PROFILE}" \
    "${CARGO_TARGET_ARGS[@]}" \
    "${CRATE_ARGS[@]}" \
    "${CARGO_EXTRA_ARGS[@]}" \
    "$@"

if [[ "${CARGO_SUBCOMMAND}" != "test" ]]; then
    if [[ -n "${STAMP}" ]]; then
        # Just touch the output as a stamp file to signal successful build
        touch "${MESON_BUILD_ROOT}/${OUTPUT}"
    else
        # Meson's @OUTPUT@ which we likely get passed includes the full path
        # but cargo doesn't honor that. So our OUTPUT maybe
        # src/something.so but cargo compiles this into debug/something.so.
        SRC="${CARGO_TARGET_DIR}/${CARGO_TARGET_SUBDIR}${CARGO_PROFILE_DIR}$(basename "$OUTPUT")"
        if [[ -d "${SRC}" ]]; then
            rm -rf "${MESON_BUILD_ROOT:?}/${OUTPUT}"
        fi
        cp -a "${SRC}" "${MESON_BUILD_ROOT}/${OUTPUT}"
        if [[ -n "${COPYDIR}" ]]; then
            mkdir -p "${MESON_BUILD_ROOT}/${COPYDIR}"
            cp -a "${SRC}" "${MESON_BUILD_ROOT}/${COPYDIR}"
        fi
    fi
fi
