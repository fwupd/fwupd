#!/bin/sh
set -e
set -x

export LC_ALL=C.UTF-8

#evaluate using Debian's build flags
eval "$(dpkg-buildflags --export=sh)"
#filter out -Bsymbolic-functions
export LDFLAGS=$(dpkg-buildflags --get LDFLAGS | sed "s/-Wl,-Bsymbolic-functions\s//")

rm -rf build
mkdir -p build
cp contrib/ci/s390x_cross.txt build/
cd build
meson .. \
	--cross-file s390x_cross.txt \
	--werror \
	-Dplugin_flashrom=false \
	-Dplugin_uefi=false \
	-Dplugin_dell=false \
	-Dplugin_modem_manager=false \
	-Dplugin_redfish=false \
	-Dintrospection=false \
	-Dgtkdoc=false \
	-Dlibxmlb:introspection=false \
	-Dlibxmlb:gtkdoc=false \
	-Dman=false
ninja -v
ninja test -v
cd ..


#test for missing translation files
./contrib/ci/check_missing_translations.sh
