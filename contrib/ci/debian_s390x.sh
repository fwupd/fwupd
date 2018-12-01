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
	-Dplugin_dell=false \
	-Dplugin_modem_manager=false \
	-Dplugin_nvme=false \
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
