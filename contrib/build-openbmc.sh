#!/bin/sh

rm -rf build-openbmc

meson setup build-openbmc \
    -Dauto_features=disabled \
    -Ddocs=disabled \
    -Dpolkit=disabled \
    -Dbash_completion=false \
    -Dfish_completion=false \
    -Dfirmware-packager=false \
    -Dhsi=disabled \
    -Dman=false \
    -Dmetainfo=false \
    -Dtests=true \
    -Dsystemd_root_prefix=/tmp \
    -Dlibxmlb:gtkdoc=false \
    $@

ninja install -C build-openbmc
build-openbmc/src/fwupdtool get-devices --verbose
test $? -eq 2 || exit 1
