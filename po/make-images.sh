#! /bin/sh
#
# make-images.sh
# Copyright (C) 2017 Peter Jones <pjones@redhat.com>
#
# Distributed under terms of the GPLv2 license.
#
install -m 0755 -d ${MESON_INSTALL_DESTDIR_PREFIX}/share/locale/
${MESON_SOURCE_ROOT}/po/make-images "Installing firmware update…" ${MESON_INSTALL_DESTDIR_PREFIX}/share/locale/ ${MESON_SOURCE_ROOT}/po/LINGUAS
for x in ${MESON_INSTALL_DESTDIR_PREFIX}/share/locale/*/LC_IMAGES/*.bmp ; do
    gzip -f ${x}
done
