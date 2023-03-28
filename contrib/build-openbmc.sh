#!/bin/sh

rm -rf build-openbmc

meson setup build-openbmc \
    -Dauto_features=disabled \
    -Ddocs=disabled \
    -Dbash_completion=false \
    -Dcompat_cli=false \
    -Dfish_completion=false \
    -Dfirmware-packager=false \
    -Dhsi=disabled \
    -Dman=false \
    -Dmetainfo=false \
    -Dtests=true \
    -Dudevdir=/tmp \
    -Dsystemd_root_prefix=/tmp \
    $@

ninja install -C build-openbmc
build-openbmc/src/fwupdtool get-devices --verbose
test $? -eq 2 || exit 1
