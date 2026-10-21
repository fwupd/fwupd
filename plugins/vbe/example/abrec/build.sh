#!/bin/sh

set -e

appstream-util validate-relax com.Vbe.Board.metainfo.xml

# The update is the image for one slot, which must start with a FIT. Here the
# FIT just holds update.bin in place of real firmware
mkimage -n "v1.2.4" -O u-boot -A arm64 -C none -T firmware -f auto -d update.bin slot.fit

fwupdtool build-cabinet Vbe-Abrec-1.2.4.cab slot.fit com.Vbe.Board.metainfo.xml
