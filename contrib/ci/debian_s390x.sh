#!/bin/sh
set -e
set -x

export LC_ALL=C.UTF-8

#evaluate using Debian's build flags
eval "$(dpkg-buildflags --export=sh)"
#filter out -Bsymbolic-functions
LDFLAGS=$(dpkg-buildflags --get LDFLAGS | sed "s/-Wl,-Bsymbolic-functions\s//")
export LDFLAGS

rm -rf build
mkdir -p build
cp contrib/ci/s390x_cross.txt build/
cd build
meson .. \
	--cross-file s390x_cross.txt \
	--werror \
	-Dplugin_flashrom=disabled \
	-Dplugin_uefi_capsule=disabled \
	-Dplugin_dell=disabled \
	-Dplugin_modem_manager=disabled \
	-Dplugin_msr=disabled \
	-Dplugin_mtd=false \
	-Dplugin_powerd=disabled \
	-Dintrospection=false \
	-Dlibxmlb:introspection=false \
	-Dlibxmlb:gtkdoc=false \
	-Dman=false
ninja -v
ninja test -v
cd ..


#test for missing translation files
./contrib/ci/check_missing_translations.sh
