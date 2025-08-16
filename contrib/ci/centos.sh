#!/bin/bash
set -e
set -x

# get any missing deps from the container
./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes -o centos

# disable the safe directory feature
git config --global safe.directory "*"

# copy current tarball, old fwupd and fwupd-efi
RPMVERSION=${VERSION//-/.}
mkdir -p $HOME/rpmbuild/SOURCES/
mv fwupd-$VERSION.tar.xz $HOME/rpmbuild/SOURCES/
cp /var/cache/*.tar.* $HOME/rpmbuild/SOURCES/

# generate a spec file and build RPM packages
mkdir -p build
sed "s,#VERSION#,$RPMVERSION,;
     s,#BUILD#,1,;
     s,#LONGDATE#,`date '+%a %b %d %Y'`,;
     s,#ALPHATAG#,alpha,;
     s,Source0.*,Source0:\tfwupd-$VERSION.tar.xz," \
	contrib/fwupd.spec.in > build/fwupd.spec
rpmbuild -ba build/fwupd.spec --with=libfwupdcompat

# copy as artifact
mkdir -p dist
cp $HOME/rpmbuild/RPMS/*/*.rpm dist
