#!/bin/sh

exec 2>&1

CAB=fakedevice124.cab
INPUT="@installedtestsdir@/fakedevice124.bin \
       @installedtestsdir@/fakedevice124.jcat \
       @installedtestsdir@/fakedevice124.metainfo.xml"

# ---
echo "Building ${CAB}..."
fwupdtool build-cabinet ${CAB} ${INPUT}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Installing ${CAB} cabinet..."
fwupdtool install ${CAB}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi
