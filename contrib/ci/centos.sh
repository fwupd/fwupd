#!/bin/sh
set -e
set -x

#clone test firmware
if [ "$CI_NETWORK" = "true" ]; then
	./contrib/ci/get_test_firmware.sh
	export G_TEST_SRCDIR=`pwd`/fwupd-test-firmware/installed-tests
fi

#build
rm -rf build
mkdir -p build
cd build
meson .. \
	--werror \
	-Dplugin_uefi_capsule=false \
	-Dplugin_dell=false \
	-Dplugin_modem_manager=false \
	-Dplugin_synaptics_mst=true \
	-Dplugin_flashrom=true \
	-Dintrospection=true \
	-Ddocs=gtkdoc \
	-Dpkcs7=false \
	-Dman=true
ninja-build -v
ninja-build test -v
cd ..
