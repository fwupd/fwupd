#!/bin/bash
appstream-util validate-relax firmware.metainfo.xml
echo -n "hello world" > firmware.bin
fwupdtool --force build-cabinet firmware.cab firmware.bin firmware.metainfo.xml
