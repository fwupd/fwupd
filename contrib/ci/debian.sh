#!/bin/bash
set -e
set -x

export QUBES_OPTION=

# remove when tpm2-tss is fixed
mkdir -p /usr/include/tss

#although it's debian, we don't build packages
if [ "$OS" = "debian-s390x" ]; then
	./contrib/ci/debian_s390x.sh
	exit 0
fi

# Set Qubes Os vars if -Dqubes=true is parameter
if [ "$QUBES" = "true" ]; then
	export QUBES_OPTION='-Dqubes=true'
fi

#prepare
export DEBFULLNAME="CI Builder"
export DEBEMAIL="ci@travis-ci.org"
VERSION=`git describe | sed 's/-/+r/;s/-/+/'`
[ -z $VERSION ] && VERSION=`head meson.build | grep ' version :' | cut -d \' -f2`
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
	./contrib/ci/ubuntu.sh
	exit 0
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
EDITOR=/bin/true dch --create --package fwupd -v $VERSION "CI Build"
debuild --no-lintian --preserve-envvar CI --preserve-envvar CC \
	--preserve-envvar QUBES_OPTION

#check lintian output
#suppress tags that are side effects of building in docker this way
lintian ../*changes \
	-IE \
	--pedantic \
	--no-tag-display-limit \
	--suppress-tags bad-distribution-in-changes-file \
	--suppress-tags debian-watch-file-in-native-package \
	--suppress-tags source-nmu-has-incorrect-version-number \
	--suppress-tags no-symbols-control-file \
	--suppress-tags gzip-file-is-not-multi-arch-same-safe \
	--suppress-tags missing-dependency-on-libc \
	--suppress-tags arch-dependent-file-not-in-arch-specific-directory \
	--allow-root

#if invoked outside of CI
if [ ! -f /.dockerenv ]; then
	echo "Not running in a container, please manually install packages"
	exit 0
fi

#test the packages install
PACKAGES=$(ls ../*.deb | grep -v 'fwupd-tests\|dbgsym')
dpkg -i $PACKAGES

# run the installed tests
if [ "$CI" = "true" ]; then
	dpkg -i ../fwupd-tests*.deb
	service dbus restart
	gnome-desktop-testing-runner fwupd
	apt purge -y fwupd-tests
fi

#test the packages remove
apt purge -y fwupd \
	     fwupd-doc \
	     libfwupd2 \
	     libfwupd-dev

#place built packages in dist outside docker
mkdir -p ../dist
cp $PACKAGES ../dist
