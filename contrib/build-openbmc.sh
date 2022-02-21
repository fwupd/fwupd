#!/bin/sh

meson ../ \
    -Dauto_features=disabled \
    -Dbash_completion=false \
    -Dcompat_cli=false \
    -Dconsolekit=false \
    -Ddocs=none \
    -Delogind=false \
    -Dfish_completion=false \
    -Dfirmware-packager=false \
    -Dhsi=false \
    -Dintrospection=false \
    -Dman=false \
    -Dmetainfo=false \
    -Doffline=false \
    -Dplugin_acpi_phat=false \
    -Dplugin_amt=false \
    -Dplugin_bcm57xx=false \
    -Dplugin_cfu=false \
    -Dplugin_emmc=false \
    -Dplugin_ep963x=false \
    -Dplugin_fastboot=false \
    -Dplugin_nitrokey=false \
    -Dplugin_parade_lspcon=false \
    -Dplugin_pixart_rf=false \
    -Dplugin_powerd=false \
    -Dplugin_realtek_mst=false \
    -Dplugin_synaptics_mst=false \
    -Dplugin_synaptics_rmi=false \
    -Dplugin_thunderbolt=false \
    -Dplugin_uf2=false \
    -Dplugin_upower=false \
    -Dtests=false \
    -Dudevdir=/tmp \
    -Dsystemd_root_prefix=/tmp \
    $@
