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
pacman -S --noconfirm ibm-sw-tpm2 tpm2-tools
tpm_server &
trap "kill $!" EXIT
# extend a PCR0 value for test suite
sleep 2
tpm2_startup -c
tpm2_pcrextend 0:sha1=f1d2d2f924e986ac86fdf7b36c94bcdf32beec15
export TPM_SERVER_RUNNING=1

# build the package and install it
sudo -E -u nobody PKGEXT='.pkg.tar' makepkg -e --noconfirm
pacman -U --noconfirm *.pkg.*

# move the package to working dir
mv *.pkg.* ../dist

# no testing here because gnome-desktop-testing isn’t available in Arch
