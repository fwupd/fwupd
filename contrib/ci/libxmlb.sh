#!/bin/bash
set -e
set -x

# download libxmlb
if [ ! -d "libxmlb" ]; then
    git clone https://github.com/hughsie/libxmlb.git
fi
pushd libxmlb

# build from a tag
git checkout 0.1.2

# build and install it
rm -rf build
mkdir -p build
export INTROSPECTION="true"
if [ "$OS" = "debian-s390x" ]; then
    cp ../contrib/ci/s390x_cross.txt build
    export CROSS="--cross-file s390x_cross.txt --libdir /usr/lib/s390x-linux-gnu/"
    export INTROSPECTION="false"
fi
pushd build
meson .. \
    $CROSS \
    --buildtype=plain \
    --prefix=/usr \
    --sysconfdir=/etc \
    --localstatedir=/var \
    --sharedstatedir=/var/lib \
    -Dintrospection=$INTROSPECTION \
    -Dgtkdoc=false \
     $@
if [ -e /usr/bin/ninja ]; then
    ninja install
else
    ninja-build install
fi
popd

popd
