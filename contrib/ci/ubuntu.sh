#!/bin/sh
set -e
set -x

#evaluate using Ubuntu's buildflags
#evaluate using Debian/Ubuntu's buildflags
#disable link time optimization, Ubuntu currently only sets it for GCC
export DEB_BUILD_MAINT_OPTIONS="optimize=-lto"
eval "$(dpkg-buildflags --export=sh)"
#filter out -Bsymbolic-functions
export LDFLAGS=$(dpkg-buildflags --get LDFLAGS | sed "s/-Wl,-Bsymbolic-functions\s//")

rm -rf build
meson build -Dman=false -Dgtkdoc=true -Dgusb:tests=false
#build with clang
ninja -C build test -v

#make docs available outside of docker
ninja -C build install -v
mkdir -p dist/docs
cp build/docs/* dist/docs -R

#run static analysis (these mostly won't be critical)
if [ "$CC" = "clang" ]; then
	ninja -C build scan-build -v
fi
