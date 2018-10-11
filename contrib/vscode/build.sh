#!/bin/sh
# Copyright (C) 2018 Dell, Inc.

SOURCE=$(dirname $0)
ROOT=$1
if [ -z "$ROOT" ]; then
        ROOT=`pwd`
fi

# build in tree
rm -rf build ${ROOT}/dist
meson build --prefix=${ROOT}/dist -Dsystemd=false -Dudevdir=${ROOT}/dist
ninja -C build install

#create helper scripts
TEMPLATE=${SOURCE}/launcher.sh
sed "s,#ROOT#,${ROOT},; s,#EXECUTABLE#,libexec/fwupd/fwupd," \
        ${TEMPLATE} > ${ROOT}/dist/fwupd.sh
sed "s,#ROOT#,${ROOT},; s,#EXECUTABLE#,libexec/fwupd/fwupdtool," \
        ${TEMPLATE} > ${ROOT}/dist/fwupdtool.sh
sed "s,#ROOT#,${ROOT},; s,#EXECUTABLE#,bin/fwupdmgr," \
        ${TEMPLATE} > ${ROOT}/dist/fwupdmgr.sh
chmod +x ${ROOT}/dist/*.sh

#create debugging targets
TARGET=${ROOT}/.vscode
mkdir -p ${TARGET}
if [ -f ${TARGET}/launch.json ]; then
        echo "${TARGET}/launch.json already exists, not overwriting"
else
        cp ${SOURCE}/launch.json ${TARGET}
fi
