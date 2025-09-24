#!/bin/bash
set -e
set -x

export QUBES_OPTION=


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
[ -z $VERSION ] && VERSION=`head meson.build | grep ' version:' | cut -d \' -f2`
rm -rf build/
mkdir -p build
shopt -s extglob
cp -R !(build|dist|venv) build/
pushd build
mv contrib/debian .
sed s/quilt/native/ debian/source/format -i
#generate control file
./contrib/ci/generate_debian.py

#check if we have all deps available
apt update -qq && apt install python3-apt -y
./contrib/ci/fwupd_setup_helpers.py install-dependencies -o debian --yes || true
dpkg-checkbuilddeps

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
	--suppress-tags missing-build-dependency-for-dh-addon \
	--suppress-tags library-not-linked-against-libc \
	--suppress-tags bad-distribution-in-changes-file \
	--suppress-tags source-nmu-has-incorrect-version-number \
	--suppress-tags no-symbols-control-file \
	--suppress-tags gzip-file-is-not-multi-arch-same-safe \
	--suppress-tags missing-dependency-on-libc \
	--suppress-tags arch-dependent-file-not-in-arch-specific-directory \
	--suppress-tags package-installs-ieee-data \
	--allow-root

#place built packages in dist outside docker
mkdir -p ../dist
PACKAGES=$(find .. -type f -name "*deb")
cp $PACKAGES ../dist
