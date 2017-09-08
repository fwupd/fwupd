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
	-Denable-werror=true \
	-Denable-uefi=false \
	-Denable-uefi-labels=false \
	-Denable-dell=false \
	-Denable-synaptics=false \
	-Denable-introspection=false \
	-Denable-doc=false \
	-Denable-man=false
ninja -v
ninja test -v
cd ..
