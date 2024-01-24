#!/bin/bash
set -e
set -x

#get any missing deps from the container
./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes -o fedora

# disable the safe directory feature
git config --global safe.directory "*"

if [ "$QUBES" = "true" ]; then
    QUBES_MACRO=(--define "qubes_packages 1")
fi

# do the full-fat build
mkdir -p build && pushd build
rm -rf *
meson setup \
    -Db_sanitize=undefined \
    $@
ninja-build
../contrib/ci/check-unused.py
ninja-build dist
popd
VERSION=`meson introspect build --projectinfo | jq -r .version`
RPMVERSION=${VERSION//-/.}
mkdir -p $HOME/rpmbuild/SOURCES/
mv build/meson-dist/fwupd-$VERSION.tar.xz $HOME/rpmbuild/SOURCES/

#generate a spec file
sed "s,#VERSION#,$RPMVERSION,;
     s,#BUILD#,1,;
     s,#LONGDATE#,`date '+%a %b %d %Y'`,;
     s,#ALPHATAG#,alpha,;
     s,enable_dummy 0,enable_dummy 1,;
     s,Source0.*,Source0:\tfwupd-$VERSION.tar.xz," \
	build/contrib/fwupd.spec.in > build/fwupd.spec

if [ -n "$CI" ]; then
	sed -i "s,enable_ci 0,enable_ci 1,;" build/fwupd.spec
fi

#build RPM packages
rpmbuild -ba "${QUBES_MACRO[@]}" build/fwupd.spec

mkdir -p dist
cp $HOME/rpmbuild/RPMS/*/*.rpm dist
