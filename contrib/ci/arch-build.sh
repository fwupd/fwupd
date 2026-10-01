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
# enabled so we can produce a report below
sudo -E -u nobody PKGEXT='.pkg.tar' makepkg -e --noconfirm

# move the package to artifact dir
mkdir -p ../dist
mv ./*.pkg.* ../dist

# generate the coverage report from the tree the tests just populated; makepkg
# builds out-of-source in build/src/build from the source in build/src/fwupd
if [ -n "$CI" ]; then
    popd
    COVERAGE_STRIP_PREFIX="build/src/fwupd/" ./contrib/ci/coverage.sh
fi
