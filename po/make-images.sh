#! /bin/sh
#
# make-images.sh
# Copyright (C) 2017 Peter Jones <pjones@redhat.com>
#
# Distributed under terms of the GPLv2 license.
#

LOCALEDIR="${DESTDIR}$1"
PYTHON3="$2"

install -m 0755 -d $LOCALEDIR
${PYTHON3} ${MESON_SOURCE_ROOT}/po/make-images "Installing firmware update…" $LOCALEDIR ${MESON_SOURCE_ROOT}/po/LINGUAS
for x in ${LOCALEDIR}/*/LC_IMAGES/*.bmp ; do
    gzip -fn9 ${x}
done
