#!/bin/bash
# Copyright 2026 Red Hat
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Usage: generate-sys-crates.sh fwupd-sys|fwupdplugin-sys <builddir>
#
# Re-generate the FFI for the given -sys crate. This uses the
# gir tool to generate Rust FFI using the .gir files in
# the meson build dir and the system-installed .gir files
# (for our dependencies)
#
# See https://gtk-rs.org/gir/book/ for documentation

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    # Prints anything from the first line that is '#\n' to the first empty line
    sed -n '/^#$/ { :a; n; /^$/q; s/^#[ ]\?//p; ba }' "${BASH_SOURCE[0]}"
}

while [[ $# -gt 0 ]]; do
    case $1 in
    --help | -h)
        usage
        exit 0
        ;;
    --*)
        echo "Unknow commandline argument $1"
        exit 1
        ;;
    *)
        break
        ;;
    esac
done

if [[ $# -ne 2 ]]; then
    usage
    exit 1
fi

MODULE="$1"
shift
FWUPD_BUILD_DIR="${1:-}"
shift

case "$MODULE" in
fwupd-sys | fwupdplugin-sys) ;;
*)
    echo "Unknown module $MODULE"
    exit 1
    ;;
esac

set -e

REQUIRED_PROGRAMS="sed pkg-config gir"
for prog in $REQUIRED_PROGRAMS; do
    if ! command -v "$prog" >/dev/null; then
        echo "Error: program $prog is missing, please install first"
        exit 1
    fi
done

if ! pkg-config gobject-introspection-1.0; then
    echo "Error: gobject-introspection-1.0.pc not found, please install first"
    exit 1
fi

set -eu

if [ -z "${FWUPD_BUILD_DIR}" ]; then
    usage
    exit 1
fi

REQUIRED_GIRS="libfwupd/Fwupd-2.0.gir libfwupdplugin/FwupdPlugin-1.0.gir"
for gir in $REQUIRED_GIRS; do
    if [ ! -f "${FWUPD_BUILD_DIR}/${gir}" ]; then
        echo "Error: ${FWUPD_BUILD_DIR}/${gir} not found. Build fwupd first."
        exit 1
    fi
done

GIR_DIR=$(pkg-config gobject-introspection-1.0 --variable girdir)

gir \
    --girs-directories "${FWUPD_BUILD_DIR}/libfwupd" \
    --girs-directories "${FWUPD_BUILD_DIR}/libfwupdplugin" \
    --girs-directories "${GIR_DIR}" \
    --config "${SCRIPT_DIR}/${MODULE}/Gir.toml" \
    --target "${SCRIPT_DIR}/${MODULE}" \
    --mode "sys"

LIB_RS="${SCRIPT_DIR}/${MODULE}/src/lib.rs"

# Post-generation fixups for limitations of the gir tool.

if [ "$MODULE" = "fwupd-sys" ]; then
    # The C headers use G_GNUC_FLAG_ENUM to force guint64 storage for
    # several flag enums, even when all member values fit in 32 bits.
    # The GIR format doesn't convey the storage size, and the gir tool
    # only promotes to u64 when member values exceed u32::MAX. Fix the
    # remaining types that are guint64 in C but c_uint in the bindings.
    for type in FwupdDeviceProblem FwupdFeatureFlags \
        FwupdInstallFlags FwupdPluginFlags FwupdReleaseFlags \
        FwupdReportFlags FwupdRequestFlags FwupdSelfSignFlags; do
        sed -i "s/pub type ${type} = c_uint;/pub type ${type} = u64;/" "$LIB_RS"
    done

    # The GI scanner sees fwupd_guid_t as guint8* (a pointer) because
    # fwupd-common.h uses #ifdef __GI_SCANNER__ to hide the fixed-size
    # array typedef. The real C type is guint8[16].
    sed -i 's/pub type fwupd_guid_t = \*mut u8;/pub type fwupd_guid_t = [u8; 16];/' "$LIB_RS"
fi

if [ "$MODULE" = "fwupdplugin-sys" ]; then
    # The gir tool generates 'use xmlb_sys as xmlb' but we use the
    # published libxmlb-sys crate which has a different crate name.
    sed -i 's/use xmlb_sys as xmlb;/use libxmlb_sys as xmlb;/' "$LIB_RS"
fi

# Fix Cargo.toml dependencies: replace git URLs with crates.io versions
# or local path references. The gir tool generates git URLs by default.
CARGO_TOML="${SCRIPT_DIR}/${MODULE}/Cargo.toml"
sed -i 's|^git = "https://github.com/gtk-rs/gtk-rs-core"$|version = "0.20"|' "$CARGO_TOML"
if [ "$MODULE" = "fwupdplugin-sys" ]; then
    sed -i '/^\[dependencies\.xmlb-sys\]$/,/^$/ { s|^\[dependencies\.xmlb-sys\]$|[dependencies.libxmlb-sys]|; s|^git = .*$|version = "0.4"|; }' "$CARGO_TOML"
    sed -i '/^\[dependencies\.fwupd-sys\]$/,/^$/ { s|^git = .*$|path = "../fwupd-sys"|; }' "$CARGO_TOML"
    # The gir tool generates the lib/package name as fwupd_plugin_sys but
    # the fwupdplugin crate expects fwupdplugin_sys.
    sed -i 's|^name = "fwupd-plugin-sys"$|name = "fwupdplugin-sys"|' "$CARGO_TOML"
    sed -i 's|^name = "fwupd_plugin_sys"$|name = "fwupdplugin_sys"|' "$CARGO_TOML"
    # Fix the test file import too
    sed -i 's|use fwupd_plugin_sys::\*;|use fwupdplugin_sys::*;|' \
        "${SCRIPT_DIR}/${MODULE}/tests/abi.rs"
fi

echo "Generated ${MODULE} successfully. Post-generation fixups applied."
echo ""
if [ "$MODULE" = "fwupdplugin-sys" ]; then
    echo "Manual steps still required:"
    echo "  - Replace the FuDeviceClass struct in src/lib.rs with the manual"
    echo "    definition that includes all vtable fields (the GIR-generated"
    echo "    version only has parent_class because fu-device.h guards the"
    echo "    vfuncs with #ifndef __GI_SCANNER__)."
fi
