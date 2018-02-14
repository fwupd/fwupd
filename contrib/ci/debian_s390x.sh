#!/bin/sh
set -e
set -x

export LC_ALL=C.UTF-8
rm -rf build
mkdir -p build
cp contrib/ci/s390x_cross.txt build/
cd build
meson .. \
	--cross-file s390x_cross.txt \
	--werror \
	-Dplugin_uefi=false \
	-Dplugin_uefi_labels=false \
	-Dplugin_dell=false \
	-Dplugin_synaptics=false \
	-Dintrospection=false \
	-Dgtkdoc=false \
	-Dman=false
ninja -v
ninja test -v
cd ..
