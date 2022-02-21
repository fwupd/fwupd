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
 -Dconsolekit=false \
 -Dsystemd=disabled \
 -Doffline=false \
 -Delogind=true
ninja -C build test -v
