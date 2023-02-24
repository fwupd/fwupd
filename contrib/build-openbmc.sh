#!/bin/sh

rm -rf build

meson setup build \
    -Dauto_features=disabled \
    -Ddocs=disabled \
    -Dbash_completion=false \
    -Dcompat_cli=false \
    -Dfish_completion=false \
    -Dfirmware-packager=false \
    -Dhsi=disabled \
    -Dman=false \
    -Dmetainfo=false \
    -Dtests=false \
    -Dudevdir=/tmp \
    -Dsystemd_root_prefix=/tmp \
    $@

ninja install -C build
build/src/fwupdtool get-devices --verbose
test $? -eq 2 || exit 1
