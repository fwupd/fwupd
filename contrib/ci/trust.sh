#!/bin/sh
set -e
set -x

#clone test firmware
if [ "$CI_NETWORK" = "true" ]; then
	./contrib/ci/get_test_firmware.sh
	export G_TEST_SRCDIR=`pwd`/fwupd-test-firmware/installed-tests
fi

# Builds using GPG and PKCS7 turned off to make
# sure no assumptions of a trust backend
rm -rf build
meson build \
	-Dman=false \
	-Ddaemon=true \
	-Dpolkit=false \
	-Dgusb:tests=false \
	-Dtpm=false \
	-Dplugin_modem_manager=false \
	-Dplugin_flashrom=false \
	-Dplugin_uefi=false \
	-Dplugin_dell=false \
	-Dplugin_redfish=false \
	-Dsystemd=false \
	-Dlvfs=false \
	-Dlibxmlb:gtkdoc=false \
	-Dlibxmlb:introspection=false \
	-Dlibjcat:introspection=false \
	-Dlibjcat:gtkdoc=false \
	-Dlibjcat:gpg=false \
	-Dlibjcat:pkcs7=false
ninja -C build test -v
