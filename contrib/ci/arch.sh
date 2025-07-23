#!/bin/bash
set -e
set -x
shopt -s extglob

#refresh package cache and update image
pacman -Syu --noconfirm

#install anything missing from the container
./contrib/ci/fwupd_setup_helpers.py install-dependencies -o arch

# check that we got the bare minimum
if [ ! -f /usr/bin/git ]; then
    echo "git not found, pacman possibly failed?"
    exit 1
fi

# prepare the build tree
rm -rf build
mkdir build && pushd build
cp ../contrib/PKGBUILD .
mkdir -p src/fwupd && pushd src/fwupd
cp -R ../../../!(build|dist) .
popd
chown nobody . -R

# build the package
sudo -E -u nobody PKGEXT='.pkg.tar' makepkg -e --noconfirm --nocheck

# move the package to artifact dir
mkdir -p ../dist
mv *.pkg.* ../dist
