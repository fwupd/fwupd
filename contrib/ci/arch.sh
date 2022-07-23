#!/bin/bash
set -e
set -x
shopt -s extglob

#clone test firmware if necessary
. ./contrib/ci/get_test_firmware.sh

#refresh package cache and update image
pacman -Syu --noconfirm

#install anything missing from the container
./contrib/ci/fwupd_setup_helpers.py install-dependencies -o arch

# prepare the build tree
rm -rf build
mkdir build && pushd build
cp ../contrib/PKGBUILD .
mkdir -p src/fwupd && pushd src/fwupd
cp -R ../../../!(build|dist) .
popd
chown nobody . -R

# install and run the custom redfish simulator
pacman -S --noconfirm python-flask
../plugins/redfish/tests/redfish.py &

# install and run TPM simulator necessary for plugins/uefi-capsule/uefi-self-test
pacman -S --noconfirm swtpm tpm2-tools
swtpm socket --tpm2 --server port=2321 --ctrl type=tcp,port=2322 --flags not-need-init --tpmstate "dir=$PWD" &
trap 'kill $!' EXIT
# extend a PCR0 value for test suite
sleep 2
tpm2_startup -c
tpm2_pcrextend 0:sha1=f1d2d2f924e986ac86fdf7b36c94bcdf32beec15
export TPM_SERVER_RUNNING=1

# build the package and install it
sudo -E -u nobody PKGEXT='.pkg.tar' makepkg -e --noconfirm
pacman -U --noconfirm *.pkg.*

#run the CI tests for Qt5
pacman -S --noconfirm qt5-base
meson qt5-thread-test ../contrib/ci/qt5-thread-test
ninja -C qt5-thread-test test

#run the CI tests for making sure we can link fwupd/fwupdplugin
meson out-of-tree-link ../contrib/ci/out-of-tree-link
ninja -C out-of-tree-link test

# move the package to working dir
mv *.pkg.* ../dist

# no testing here because gnome-desktop-testing isn’t available in Arch
