#!/bin/sh
set -e
set -x

rm -rf build
mkdir -p build
cd build
meson .. \
	--werror \
	-Dplugin_uefi=false \
	-Dplugin_uefi_labels=false \
	-Dplugin_dell=true \
	-Dplugin_synaptics=true \
	-Dintrospection=true \
	-Dgtkdoc=true \
	-Dpkcs7=false \
	-Dman=true
ninja-build -v
ninja-build test -v
cd ..
