#!/bin/sh
set -e
export LC_ALL=C.UTF-8
mkdir -p build && cd build
rm * -rf
meson .. \
    -Denable-doc=true \
    -Denable-man=true \
    -Denable-tests=true \
    -Denable-thunderbolt=false \
    -Denable-uefi=true \
    -Denable-dell=true \
    -Denable-synaptics=true \
    -Denable-colorhug=true $@
ninja -v || bash
ninja test -v
DESTDIR=/tmp/install-ninja ninja install
cd ..
