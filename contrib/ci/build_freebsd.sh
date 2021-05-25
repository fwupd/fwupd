#!/bin/sh
set -e
export LC_ALL=C.UTF-8
mkdir -p build && cd build
rm -rf *
meson .. \
    -Dgtkdoc=true \
    -Dtests=true \
    -Dgudev=false \
    -Dlibjcat:gpg=false \
    -Dgusb:usb_ids=/usr/local/share/usbids/usb.ids \
    -Dplugin_altos=false \
    -Dplugin_amt=false \
    -Dplugin_dell=false \
    -Dplugin_emmc=false \
    -Dplugin_nvme=false \
    -Dplugin_synaptics_mst=false \
    -Dplugin_synaptics_rmi=false \
    -Dplugin_thunderbolt=false \
    -Dplugin_tpm=false \
    -Dplugin_uefi_capsule=false \
    -Dsystemd=false \
    $@
ninja -v || sh
ninja test -v
DESTDIR=/tmp/install-ninja ninja install
cd ..
