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
echo "Examining ${CAB}..."
fwupdtool get-details ${CAB}
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

# ---
echo "Getting history (should be one)..."
fwupdtool get-history
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Clearing history..."
fwupdtool clear-history ${DEVICE}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting history (should be none)..."
fwupdtool get-history
rc=$?; if [ $rc != 2 ]; then exit $rc; fi

# ---
echo "Resetting config..."
fwupdtool reset-config test
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Testing good version compare"
fwupdtool vercmp 1.0.0 1.0.0 triplet
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Testing bad version compare"
fwupdtool vercmp 1.0.0 1.0.1 foo
rc=$?; if [ $rc != 1 ]; then exit $rc; fi

# ---
echo "Getting supported version formats..."
fwupdtool get-version-formats --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi
