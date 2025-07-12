#!/bin/sh
set -e
fwupdtool firmware-build uswid.builder.xml firmware.bin --force
fwupdtool build-cabinet mtd-test-1.2.3.cab firmware.bin org.fwupd.mtd-test.metainfo.xml
