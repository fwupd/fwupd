#!/bin/bash
set -e
set -x
shopt -s extglob

VERSION=`git describe | sed 's/-/.r/;s/-/./'`
[ -z $VERSION ] && VERSION=`head meson.build | grep ' version :' | cut -d \' -f2`

#install anything missing from the container
./contrib/ci/generate_dependencies.py | xargs pacman -S --noconfirm --needed

# prepare the build tree
rm -rf build
mkdir build && pushd build
cp ../contrib/PKGBUILD .
sed -i "s,#VERSION#,$VERSION," PKGBUILD
mkdir -p src/fwupd && pushd src/fwupd
cp -R ../../../!(build|dist) .
popd
chown nobody . -R

# build the package and install it
sudo -E -u nobody makepkg -e --noconfirm
pacman -U --noconfirm *.pkg.tar.xz

# move the package to working dir
mv *.pkg.tar.xz ../dist

# no testing here because gnome-desktop-testing isn’t available in Arch
