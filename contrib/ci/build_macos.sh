#!/bin/bash
set -e
set -x

export PKG_CONFIG_PATH="/opt/homebrew/Cellar/readline/8.2.13/lib/pkgconfig:$PKG_CONFIG_PATH"

mkdir -p build-macos && cd build-macos
meson setup .. \
    -Dbuild=all \
    -Ddbus_socket_address="unix:path=/var/run/fwupd.socket" \
    -Dman=false \
    -Dlibjcat:gpg=false \
    -Dlibxmlb:gtkdoc=false \
    $@
