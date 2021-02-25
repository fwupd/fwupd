#!/bin/bash
set -e
set -x

# check for g_return_val_if_fail sanity
if ! ./contrib/ci/check-null-false-returns.py; then
    exit 1
fi

# these are deprecated in favor of INTERNAL flags
deprecated="FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS
            FWUPD_DEVICE_FLAG_ONLY_SUPPORTED
            FWUPD_DEVICE_FLAG_MD_SET_NAME
            FWUPD_DEVICE_FLAG_MD_SET_VERFMT
            FWUPD_DEVICE_FLAG_NO_GUID_MATCHING
            FWUPD_DEVICE_FLAG_MD_SET_ICON"
for val in $deprecated; do
    if grep -- $val plugins/*/*.c ; then
        exit 1
    fi
done

# check shell scripts
dnf install -y ShellCheck
if ! find . -name '*.sh' | xargs shellcheck --severity=error -e SC2068; then
    exit 1
fi

#get any missing deps from the container
./contrib/ci/generate_dependencies.py | xargs dnf install -y

#generate a tarball
git config tar.tar.xz.command "xz -c"
mkdir -p build && pushd build
rm -rf *

if [ "$QUBES" = "true" ]; then
    QUBES_MACRO=(--define "qubes_packages 1")
fi

meson .. \
    -Dgtkdoc=true \
    -Dman=true \
    -Dtests=true \
    -Dgusb:tests=false \
    -Dplugin_dummy=true \
    -Dplugin_flashrom=true \
    -Dplugin_modem_manager=false \
    -Dplugin_thunderbolt=true \
    -Dplugin_uefi_capsule=true \
    -Dplugin_dell=true \
    -Dplugin_synaptics_mst=true $@
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
	contrib/fwupd.spec.in > build/fwupd.spec

if [ -n "$CI" ]; then
	sed -i "s,enable_ci 0,enable_ci 1,;" build/fwupd.spec
fi

#build RPM packages
rpmbuild -ba "${QUBES_MACRO[@]}" build/fwupd.spec

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
	sed "s,^DisabledPlugins=.*,DisabledPlugins=," -i /etc/fwupd/daemon.conf

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
