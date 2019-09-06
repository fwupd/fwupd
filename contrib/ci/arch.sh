#!/bin/bash
set -e
set -x
shopt -s extglob

# prepare the build tree
rm -rf build
mkdir build && pushd build
cp ../contrib/PKGBUILD .
mkdir -p src/fwupd && pushd src/fwupd
cp -R ../../../!(build|dist) .
popd
chown nobody . -R

# install and run TPM simulator necessary for plugins/uefi/uefi-self-test
pacman -S --noconfirm ibm-sw-tpm2
tpm_server &
trap "kill $!" EXIT
export TPM_SERVER_RUNNING=1

# build the package and install it
sudo -E -u nobody PKGEXT='.pkg.tar' makepkg -e --noconfirm
pacman -U --noconfirm *.pkg.*

# move the package to working dir
mv *.pkg.* ../dist

# no testing here because gnome-desktop-testing isn’t available in Arch
