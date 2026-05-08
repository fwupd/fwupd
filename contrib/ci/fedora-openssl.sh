#!/bin/sh
set -eux

# check for and install missing dependencies
./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes -o fedora

# make sure gnutls is not available
dnf remove -y gnutls-devel

root=$(pwd)
export BUILD=${root}/build

rm -rf "$BUILD"
meson "$BUILD" \
    -Db_coverage=true \
    -Dman=false \
    -Ddocs=disabled \
    -Dgnutls=disabled \
    -Dlibxmlb:gtkdoc=false \
    --prefix="$root/target"
ninja -C "$BUILD" -v
meson test -C "$BUILD" --print-errorlogs --verbose

# generate coverage report
ninja -C "$BUILD" coverage-xml
cp "$BUILD/meson-logs/coverage.xml" .
