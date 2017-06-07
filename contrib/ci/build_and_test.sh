#!/bin/sh
set -e

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
ninja -v
ninja test -v
DESTDIR=/tmp/install-ninja ninja install
cd ..
