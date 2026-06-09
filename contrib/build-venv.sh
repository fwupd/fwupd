#!/bin/sh
set -e

. "$(dirname "$(readlink -f "$0")")/nix.sh"

VENV=$(dirname "$0")/..
BUILD=${VENV}/build
DIST=${VENV}/dist
EXTRA_ARGS="-Dlibxmlb:gtkdoc=false -Dsystemd=disabled"

# NixOS: extract vendor_ids_dir from mesonFlags set by nix-shell
# plugin_uefi_capsule_splash disabled, same as in nixpkgs
if [ -f "${VENV}/.nixos" ] && [ -n "$mesonFlags" ]; then
    for flag in $mesonFlags; do
        case "$flag" in
        -Dvendor_ids_dir=* | -Dplugin_uefi_capsule_splash=*) EXTRA_ARGS="$EXTRA_ARGS $flag" ;;
        esac
    done
fi

#build and install
if [ ! -d ${BUILD} ] || ! [ -e ${BUILD}/build.ninja ]; then
    meson setup ${BUILD} --prefix=${DIST} ${EXTRA_ARGS} $@
fi
ninja -C ${BUILD} install

# check whether we have an existing fwupd EFI binary in the host system to use
EFI_PREFIX=$(pkg-config fwupd-efi --variable=prefix 2>/dev/null || echo "/usr")
EFI_DIR=libexec/fwupd/efi
if [ -d "${EFI_PREFIX}/${EFI_DIR}" ]; then
    BINARIES=$(find "${EFI_PREFIX}/${EFI_DIR}" -name "*.efi*" -type f -print || true)
    if [ -n "${BINARIES}" ]; then
        mkdir -p ${DIST}/${EFI_DIR}
        for i in ${BINARIES}; do
            if [ -f "${DIST}/${EFI_DIR}/$(basename $i)" ]; then
                continue
            fi
            ln -s $i "${DIST}/${EFI_DIR}/$(basename $i)"
        done
    fi
fi
