#!/bin/sh
set -e
set -x

rm -rf build
meson build --werror
#build with clang and -Werror
ninja -C build -v
#run static analysis (these mostly won't be critical)
ninja -C build scan-build -v
