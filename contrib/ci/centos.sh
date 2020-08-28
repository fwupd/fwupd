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
	-Dplugin_uefi=false \
	-Dplugin_dell=false \
	-Dplugin_modem_manager=false \
	-Dplugin_synaptics=true \
	-Dplugin_flashrom=true \
	-Dintrospection=true \
	-Dgtkdoc=true \
	-Dpkcs7=false \
	-Dman=true
ninja-build -v
ninja-build test -v
cd ..
