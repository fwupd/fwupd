#!/bin/bash
set -e
set -x

#generate a tarball
git config tar.tar.xz.command "xz -c"
mkdir -p build && pushd build
rm -rf *
meson .. \
    -Db_sanitize=address \
    -Dgtkdoc=true \
    -Dman=true \
    -Dtests=true \
    -Dplugin_dummy=true \
    -Dplugin_flashrom=true \
    -Dplugin_modem_manager=false \
    -Dplugin_thunderbolt=true \
    -Dplugin_uefi=true \
    -Dplugin_dell=true \
    -Dplugin_synaptics=true $@
ninja-build dist
popd
VERSION=`./contrib/get-version.py`
RPMVERSION=${VERSION//-/.}
mkdir -p $HOME/rpmbuild/SOURCES/
mv build/meson-dist/fwupd-$VERSION.tar.xz $HOME/rpmbuild/SOURCES/

#generate a spec file
sed "s,#VERSION#,$RPMVERSION,;
     s,#TARBALL_VERSION#,$VERSION,;
     s,#BUILD#,1,;
     s,#LONGDATE#,`date '+%a %b %d %Y'`,;
     s,#ALPHATAG#,alpha,;
     s,enable_dummy 0,enable_dummy 1,;
     s,Source0.*,Source0:\tfwupd-$VERSION.tar.xz," \
	contrib/fwupd.spec.in > build/fwupd.spec

if [ -n "$CI" ]; then
	sed -i "s,enable_ci 0,enable_ci 1,;" build/fwupd.spec
fi

#build RPM packages
rpmbuild -ba build/fwupd.spec

#if invoked outside of CI
if [ ! -f /.dockerenv ]; then
        echo "Not running in a container, please manually install packages"
        exit 0
fi

#install RPM packages
dnf install -y $HOME/rpmbuild/RPMS/*/*.rpm

mkdir -p dist
cp $HOME/rpmbuild/RPMS/*/*.rpm dist

if [ "$CI" = "true" ]; then
	sed "s,^BlacklistPlugins=test;invalid,BlacklistPlugins=," -i /etc/fwupd/daemon.conf

	# set up enough PolicyKit and D-Bus to run the daemon
	mkdir -p /run/dbus
	mkdir -p /var
	ln -s /var/run /run
	dbus-daemon --system --fork
	/usr/lib/polkit-1/polkitd &
	sleep 5

	# run the daemon startup to check it can start
	/usr/libexec/fwupd/fwupd --immediate-exit --verbose

	# run the installed tests whilst the daemon debugging
	/usr/libexec/fwupd/fwupd --verbose &
	sleep 10
	gnome-desktop-testing-runner fwupd
fi
