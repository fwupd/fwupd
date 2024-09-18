#!/bin/bash
set -e
set -x

mkdir -p build-macos && cd build-macos
meson setup .. \
    -Dbuild=all \
    -Ddbus_socket_address="unix:path=/var/run/fwupd.socket" \
    -Dman=false \
    -Dlibjcat:gpg=false \
    -Dlibxmlb:gtkdoc=false \
    $@
