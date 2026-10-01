#!/usr/bin/env bash
set -e
set -x
shopt -s extglob

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

# build the package; the PKGBUILD check() runs the unit tests with coverage
# enabled so the test step can produce a report
sudo -E -u nobody HOME="$PWD" PKGEXT='.pkg.tar' makepkg -e --noconfirm

# move the package to artifact dir
mkdir -p ../dist
mv ./*.pkg.* ../dist
