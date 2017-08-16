#!/bin/bash
set -e
set -x

#build deb packages
export DEBFULLNAME="CI Builder"
export DEBEMAIL="ci@travis-ci.org"
VERSION=`git describe | sed 's/-/+r/;s/-/+/'`
[ -z $VERSION ] && VERSION=`head meson.build | grep ' version :' | cut -d \' -f2`

rm -rf build/
mkdir -p build && pushd build
ln -s ../* .
cp contrib/debian . -R
sed s/quilt/native/ debian/source/format -i
EDITOR=/bin/true dch --create --package fwupd -v $VERSION "CI Build"
debuild --no-lintian

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
	--allow-root

#test the packages install
dpkg -i `ls ../*.deb | grep -v fwupd-tests`

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
	     fwupd-dbgsym \
	     gir1.2-fwupd-1.0 \
	     libdfu1 \
	     libdfu1-dbgsym \
	     libdfu-dev \
	     libfwupd1 \
	     libfwupd1-dbgsym \
	     libfwupd-dev
