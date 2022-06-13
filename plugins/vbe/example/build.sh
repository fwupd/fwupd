#!/bin/sh

set -e

appstream-util validate-relax com.Vbe.Board.metainfo.xml

#dd if=/dev/zero of=update.bin bs=1M count=1
mkimage -D "-p 0x100" -n "v1.2.4" -O U-Boot -A arm64 -C none -T firmware -f auto -d update.bin firmware.fit
fdtput firmware.fit -t s /configurations/conf-1 version "1.2.4"

# Make the data external, now that we have finished fiddling with the FDT
mkimage -E -F firmware.fit

gcab --create --zip --nopath Vbe-Board-1.2.4.cab firmware.fit com.Vbe.Board.metainfo.xml
