#!/bin/bash -e

#build deb packages
VERSION=`git describe --abbrev=0 --tags`
rm -rf build/
mkdir -p build && pushd build
ln -s ../* .
cp contrib/debian . -R
sed s/quilt/native/ debian/source/format -i
EDITOR=/bin/true dch --create --package fwupd -v $VERSION "CI Build"
dpkg-buildpackage

#test the packages install
dpkg -i ../*.deb

# run the installed tests
/etc/init.d/dbus start
gnome-desktop-testing-runner fwupd
