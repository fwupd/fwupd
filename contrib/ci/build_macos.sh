#!/bin/bash
set -e
set -x

mkdir -p build-macos && cd build-macos
meson setup .. \
    -Dbuild=standalone \
    -Dsoup_session_compat=false \
    -Dgusb:docs=false \
    -Dlibjcat:gpg=false \
    -Dlibxmlb:gtkdoc=false \
    $@
