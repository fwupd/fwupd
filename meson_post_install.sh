#!/bin/sh
if [ -z $MESON_INSTALL_PREFIX ]; then
    echo 'This is meant to be ran from Meson only!'
    exit 1
fi

#if [ -z $DESTDIR ]; then
    echo 'Updating systemd deps'
    mkdir -p $DESTDIR/lib/systemd/system/system-update.target.wants
    ln -sf ../fwupd-offline-update.service $DESTDIR/lib/systemd/system/system-update.target.wants/fwupd-offline-update.service
#fi
