#!/bin/bash
set -e
set -x
shopt -s extglob

VERSION=`git describe | sed 's/-/.r/;s/-/./'`
[ -z $VERSION ] && VERSION=`head meson.build | grep ' version :' | cut -d \' -f2`

# prepare the build tree
rm -rf build
mkdir build && pushd build
cp ../contrib/PKGBUILD .
sed -i "s,#VERSION#,$VERSION," PKGBUILD
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
sudo -E -u nobody makepkg -e --noconfirm
pacman -U --noconfirm *.pkg.tar.xz

# move the package to working dir
mv *.pkg.tar.xz ../dist

# no testing here because gnome-desktop-testing isn’t available in Arch
