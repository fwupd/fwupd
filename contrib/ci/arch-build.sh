#!/usr/bin/env bash
set -e
set -x
shopt -s extglob

# check that we got the bare minimum
if [ ! -f /usr/bin/git ]; then
    echo "git not found, pacman possibly failed?"
    exit 1
fi

# Disable makepkg's debug file-prefix-map (remaps $srcdir to /usr/src/debug/fwupd)
# for the CI build. The remapped paths do not exist at coverage time, so gcov
# records unresolvable source paths and gcovr filters everything out. Dropping it
# makes gcov record real paths, matching the other distro CI jobs.
if [ -n "$CI" ]; then
    echo 'OPTIONS+=(!debug)' >>/etc/makepkg.conf
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
mv ./*.pkg.* ../dist
