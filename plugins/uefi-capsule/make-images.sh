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
${PYTHON3} ${MESON_SOURCE_ROOT}/plugins/uefi-capsule/make-images.py \
	--label="Installing firmware update…" \
	--localedir=$LOCALEDIR \
	--linguas=${MESON_SOURCE_ROOT}/po/LINGUAS
