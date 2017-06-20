#!/bin/bash -e

# prepare the build tree
rm -rf build
mkdir build && pushd build
cp ../contrib/PKGBUILD .
mkdir -p src/fwupd && pushd src/fwupd
ln -s ../../../* .
ln -s ../../../.git .
popd

# build the package and install it
makepkg -ei --noconfirm

# move the package to working dir
cp *.pkg.tar.xz ../

# no testing here because gnome-desktop-testing isn’t available in Arch
