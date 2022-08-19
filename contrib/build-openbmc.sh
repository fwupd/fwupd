#!/bin/sh

meson ../ \
    -Dauto_features=disabled \
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
