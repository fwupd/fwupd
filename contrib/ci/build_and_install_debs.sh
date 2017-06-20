#!/bin/bash -e

#build deb packages
VERSION=`head meson.build | sed "/version :/!d; /meson/d; s/,//; s/'//g;" | awk -F ':' '{print $2}'`
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
if [ "$CI" = "true" ]; then
	sed -i "s,Exec=,Exec=/bin/sh -c 'FWUPD_TESTS=$CI ,;
		s,Exec=.*$,&',;" \
		/usr/share/dbus-1/system-services/org.freedesktop.fwupd.service
	/etc/init.d/dbus start
	gnome-desktop-testing-runner fwupd
fi
