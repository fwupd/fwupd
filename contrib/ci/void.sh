#!/bin/sh
set -e
set -x

#install dependencies
xbps-install -Suy python3
./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes -o void

#clone test firmware if necessary
. ./contrib/ci/get_test_firmware.sh

#build
rm -rf build
meson build \
 -Dgusb:tests=false \
 -Dgcab:docs=false \
 -Dconsolekit=disabled \
 -Dsystemd=disabled \
 -Doffline=disabled \
 -Delogind=enabled
ninja -C build test -v
