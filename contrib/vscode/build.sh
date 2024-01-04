#!/bin/sh
# Copyright (C) 2018 Dell, Inc.

SOURCE=$(dirname $0)
ROOT=$(pwd)
BUILD=${ROOT}/build
DIST=${ROOT}/dist

#build and install
rm -rf ${BUILD} ${DIST}
meson setup ${BUILD} --prefix=${DIST} -Dsystemd=disabled -Dudevdir=${DIST} $@
ninja -C ${BUILD} install

#create wrapper scripts
TEMPLATE=${SOURCE}/launcher.sh
for binary in fwupdtool fwupdmgr fwupd; do
        ln -s ../${TEMPLATE} ${DIST}/$binary
done

#create debugging targets
TARGET=${ROOT}/.vscode
mkdir -p ${TARGET}
if [ -f ${TARGET}/launch.json ]; then
        echo "${TARGET}/launch.json already exists, not overwriting"
else
        cp ${SOURCE}/launch.json ${TARGET}
fi
