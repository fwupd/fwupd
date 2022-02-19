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
	-Dplugin_uefi_capsule=false \
	-Dplugin_dell=disabled \
	-Dplugin_modem_manager=false \
	-Dplugin_synaptics_mst=true \
	-Dplugin_flashrom=enabled \
	-Dintrospection=true \
	-Ddocs=gtkdoc \
	-Dpkcs7=false \
	-Dman=true
ninja-build -v
ninja-build test -v
cd ..
