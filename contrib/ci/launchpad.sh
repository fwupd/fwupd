#!/bin/bash
set -e
set -x

#prepare
export DEBFULLNAME="Sean Rhodes"
export DEBEMAIL="sean@starlabs.systems"

MAJOR=`head meson.build | grep ' version :' | cut -d \' -f2`
if [[ "$MAJOR" == "1.7.0" ]]; then
	VERSION=`echo 1.7.0.2166+f`
else
	VERSION=`echo $MAJOR+f`
fi

rm -rf build/
mkdir -p build
shopt -s extglob
cp -lR !(build|dist|venv) build/
pushd build
mv contrib/debian .
sed s/quilt/native/ debian/source/format -i
#generate control file
./contrib/ci/generate_debian.py

#check if we have all deps available
#if some are missing, we're going to use subproject instead and
#packaging CI will fail
./contrib/ci/generate_dependencies.py  | xargs apt install -y || true
if ! dpkg-checkbuilddeps; then
	# sudo add-apt-repository ppa:starlabs/fwupd
	echo "deb http://ppa.launchpad.net/starlabs/fw/ubuntu focal main" > /etc/apt/sources.list.d/starlabs.list
	apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 17A20BAF70BEC3904545ACFF8F21C26C794386E3
	apt update
	./contrib/ci/generate_dependencies.py | xargs apt install -y
fi

#clone test firmware
if [ "$CI_NETWORK" = "true" ]; then
	./contrib/ci/get_test_firmware.sh
	export G_TEST_SRCDIR=`pwd`/fwupd-test-firmware/installed-tests
fi

#disable unit tests if fwupd is already installed (may cause problems)
if [ -x /usr/lib/fwupd/fwupd ]; then
	export DEB_BUILD_OPTIONS=nocheck
fi
#build the package
EDITOR=/bin/true dch --create --package fwupd --distribution focal -v $VERSION "CI Build"

debuild -S
