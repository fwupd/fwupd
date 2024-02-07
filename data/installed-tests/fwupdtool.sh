#!/bin/sh

exec 2>&1

CAB=fakedevice124.cab
INPUT="@installedtestsdir@/fakedevice124.bin \
       @installedtestsdir@/fakedevice124.jcat \
       @installedtestsdir@/fakedevice124.metainfo.xml"
DEVICE=08d460be0f1f9f128413f816022a6439e0078018

# ---
echo "Enabling test device..."
fwupdtool enable-test-devices
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Building ${CAB}..."
fwupdtool build-cabinet ${CAB} ${INPUT} --force
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Installing ${CAB} cabinet..."
fwupdtool install ${CAB}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Cleaning ${CAB} generated cabinet ..."
rm -f ${CAB}

# ---
echo "Verifying update..."
fwupdtool verify-update ${DEVICE}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi
