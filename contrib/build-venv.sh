#!/bin/sh -e

VENV=$(dirname $0)/..
BUILD=${VENV}/build
DIST=${VENV}/dist

#build and install
if [ ! -d ${BUILD} ]; then
        meson setup ${BUILD} --prefix=${DIST} -Dsystemd=disabled -Dudevdir=${DIST} $@
fi
ninja -C ${BUILD} install

# check whether we have an existing fwupd EFI binary in the host system to use
EFI_PREFIX=$(pkg-config fwupd-efi --variable=prefix 2>/dev/null || echo "/usr")
EFI_DIR=libexec/fwupd/efi
BINARIES=$(find "${EFI_PREFIX}/${EFI_DIR}" -name "*.efi*" -type f -print)
if [ -n "${BINARIES}" ]; then
        mkdir -p ${DIST}/${EFI_DIR}
        for i in ${BINARIES}; do
                if [ -f "${DIST}/${EFI_DIR}/$(basename $i)" ]; then
                        continue
                fi
                ln -s $i "${DIST}/${EFI_DIR}/$(basename $i)"
        done
fi
