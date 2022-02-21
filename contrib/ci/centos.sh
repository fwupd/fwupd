#!/bin/sh
set -e
set -x

#clone test firmware if necessary
. ./contrib/ci/get_test_firmware.sh

#build
rm -rf build
mkdir -p build
cd build
meson .. \
	--werror \
	-Dplugin_uefi_capsule=disabled \
	-Dplugin_dell=disabled \
	-Dplugin_modem_manager=disabled \
	-Dplugin_synaptics_mst=enabled \
	-Dplugin_flashrom=enabled \
	-Dintrospection=true \
	-Ddocs=gtkdoc \
	-Dpkcs7=false \
	-Dman=true
ninja-build -v
ninja-build test -v
cd ..
