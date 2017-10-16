#!/bin/bash
set -e
set -x

#generate a tarball
git config tar.tar.xz.command "xz -c"
mkdir -p build && pushd build
rm -rf *
meson .. \
    -Denable-werror=true \
    -Denable-doc=true \
    -Denable-man=true \
    -Denable-tests=true \
    -Denable-dummy=true \
    -Denable-thunderbolt=true \
    -Denable-uefi=true \
    -Denable-dell=true \
    -Denable-synaptics=true \
    -Denable-colorhug=true $@
ninja-build dist
popd
VERSION=`mesonintrospect build --projectinfo | jq -r .version`
mkdir -p $HOME/rpmbuild/SOURCES/
mv build/meson-dist/fwupd-$VERSION.tar.xz $HOME/rpmbuild/SOURCES/

#generate a spec file
sed "s,#VERSION#,$VERSION,;
     s,#BUILD#,1,;
     s,#LONGDATE#,`date '+%a %b %d %Y'`,;
     s,#ALPHATAG#,alpha,;
     s,enable_dummy 0,enable_dummy 1,;
     s,Source0.*,Source0:\tfwupd-$VERSION.tar.xz," \
	contrib/fwupd.spec.in > build/fwupd.spec

#build RPM packages
rpmbuild -ba build/fwupd.spec

#if invoked outside of CI
if [ ! -f /.dockerenv ]; then
        echo "Not running in a container, please manually install packages"
        exit 0
fi

#install RPM packages
dnf install -y $HOME/rpmbuild/RPMS/*/*.rpm

cp $HOME/rpmbuild/RPMS/*/*.rpm .

# run the installed tests
if [ "$CI" = "true" ]; then
	sed "s,^BlacklistPlugins=test,BlacklistPlugins=," -i /etc/fwupd/daemon.conf
	mkdir -p /run/dbus
	mkdir -p /var
	ln -s /var/run /run
	dbus-daemon --system --fork
	gnome-desktop-testing-runner fwupd
fi
