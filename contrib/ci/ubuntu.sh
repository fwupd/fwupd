#!/bin/sh
set -e
set -x

#evaluate using Ubuntu's buildflags
eval "$(dpkg-buildflags --export=sh)"
#filter out -Bsymbolic-functions
export LDFLAGS=$(dpkg-buildflags --get LDFLAGS | sed "s/-Wl,-Bsymbolic-functions\s//")

rm -rf build
meson build --werror
#build with clang and -Werror
ninja -C build test -v
#run static analysis (these mostly won't be critical)
ninja -C build scan-build -v
