#!/bin/bash
set -e
set -x

#prepare
export DEBFULLNAME="CI Builder"
export DEBEMAIL="ci@travis-ci.org"
VERSION=`git describe | sed 's/-/+r/;s/-/+/'`
[ -z $VERSION ] && VERSION=`head meson.build | grep ' version :' | cut -d \' -f2`
rm -rf build/
mkdir -p build
shopt -s extglob
cp -lR !(build) build/
pushd build
mv contrib/debian .
sed s/quilt/native/ debian/source/format -i
#generate control file
./contrib/ci/generate_debian_control.py debian/control.in debian/control
#build the package
EDITOR=/bin/true dch --create --package fwupd -v $VERSION "CI Build"
debuild --no-lintian --preserve-envvar CI --preserve-envvar CC

#check lintian output
#suppress tags that are side effects of building in docker this way
lintian ../*changes \
	-IE \
	--pedantic \
	--no-tag-display-limit \
	--suppress-tags bad-distribution-in-changes-file \
	--suppress-tags source-contains-unsafe-symlink \
	--suppress-tags changelog-should-mention-nmu \
	--suppress-tags debian-watch-file-in-native-package \
	--suppress-tags source-nmu-has-incorrect-version-number \
	--suppress-tags no-symbols-control-file \
	--allow-root

#if invoked outside of CI
if [ ! -f /.dockerenv ]; then
	echo "Not running in a container, please manually install packages"
	exit 0
fi

#test the packages install
dpkg -i `ls ../*.deb | grep -v 'fwupd-tests\|dbgsym'`

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
