#!/bin/sh
if [ -z $MESON_INSTALL_PREFIX ]; then
    echo 'This is meant to be ran from Meson only!'
    exit 1
fi

SYSTEMDUNITDIR=$1
LOCALSTATEDIR=$2

#if [ -z $DESTDIR ]; then
    echo 'Updating systemd deps'
    mkdir -p ${DESTDIR}${SYSTEMDUNITDIR}/system-update.target.wants
    ln -sf ../fwupd-offline-update.service ${DESTDIR}${SYSTEMDUNITDIR}/system-update.target.wants/fwupd-offline-update.service
#fi
