#!/bin/bash -e

#generate a tarball
git config tar.tar.xz.command "xz -c"
mkdir -p build && pushd build
rm -rf *
meson .. \
    -Denable-doc=true \
    -Denable-man=true \
    -Denable-tests=true \
    -Denable-thunderbolt=false \
    -Denable-uefi=true \
    -Denable-dell=true \
    -Denable-synaptics=true \
    -Denable-colorhug=true $@
ninja-build dist
popd
VERSION=`ls | sed '/^fwupd-.*.tar.xz/!d; s,^fwupd-,,; s,.tar.xz,,'`
mkdir -p $HOME/rpmbuild/SOURCES/
mv fwupd-$VERSION.tar.xz $HOME/rpmbuild/SOURCES/

#generate a spec file
sed "s,#VERSION#,$VERSION,;
     s,enable_tests 0,enable_tests 1,;
     s,#BUILD#,1,;
     s,#LONGDATE#,`date '+%a %b %d %Y'`,;
     s,#ALPHATAG#,alpha,;
     s,Source0.*,Source0:\tfwupd-$VERSION.tar.xz," \
	contrib/fwupd.spec.in > build/fwupd.spec

#build RPM packages
rpmbuild -ba build/fwupd.spec

#install RPM packages
dnf install -C -y $HOME/rpmbuild/RPMS/*/*.rpm

cp $HOME/rpmbuild/RPMS/*/*.rpm .

# run the installed tests
mkdir -p /run/dbus
mkdir -p /var
ln -s /var/run /run
dbus-daemon --system --fork
gnome-desktop-testing-runner fwupd
