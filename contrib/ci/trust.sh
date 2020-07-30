#!/bin/sh
set -e
set -x

# Builds using GPG and PKCS7 turned off to make
# sure no assumptions of a trust backend
rm -rf build
meson build \
	-Dman=false \
	-Ddaemon=false \
	-Dplugin_tpm=false \
	-Dplugin_modem_manager=false \
	-Dplugin_flashrom=false \
	-Dplugin_uefi=false \
	-Dplugin_dell=false \
	-Dplugin_redfish=false \
	-Dsystemd=false \
	-Dlvfs=false \
	-Dlibxmlb:gtkdoc=false \
	-Dlibxmlb:introspection=false \
	-Dlibjcat:introspection=false \
	-Dlibjcat:gtkdoc=false \
	-Dlibjcat:gpg=false \
	-Dlibjcat:pkcs7=false
ninja -C build test -v
