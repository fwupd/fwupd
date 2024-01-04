#!/bin/sh -e

VENV=$(dirname $0)/..
BUILD=${VENV}/build
DIST=${VENV}/dist

#build and install
if [ ! -d ${BUILD} ]; then
        meson setup ${BUILD} --prefix=${DIST} -Dsystemd=disabled -Dudevdir=${DIST} $@
fi
ninja -C ${BUILD} install
