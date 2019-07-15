#!/bin/sh
set -e
set -x

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
