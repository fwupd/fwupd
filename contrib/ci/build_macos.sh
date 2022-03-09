#!/bin/bash
set -e
set -x

mkdir -p build-macos && cd build-macos
meson .. \
    -Dbuild=standalone \
    -Ddocs=none \
    -Dhsi=false \
    -Dsoup_session_compat=false \
    -Dgusb:docs=false \
    -Dlibjcat:gpg=false \
    -Dlibxmlb:gtkdoc=false \
    $@
