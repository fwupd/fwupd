#!/bin/bash

dirname=`dirname $0`

# ---
echo "Getting the list of remotes..."
fwupdmgr get-remotes
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Refreshing with dummy metadata..."
fwupdmgr refresh ${dirname}/firmware-example.xml.gz ${dirname}/firmware-example.xml.gz.asc lvfs
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Update the device hash database..."
fwupdmgr verify-update
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Getting devices (should be one)..."
fwupdmgr get-devices
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Testing the verification of firmware..."
fwupdmgr verify
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Getting updates (should be one)..."
fwupdmgr get-updates
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Installing test firmware..."
fwupdmgr install ${dirname}/fakedevice124.cab
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Getting updates (should be none)..."
fwupdmgr get-updates
rc=$?; if [[ $rc != 2 ]]; then exit $rc; fi

# ---
echo "Testing the verification of firmware (again)..."
fwupdmgr verify
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Downgrading to older release (requires network access)"
fwupdmgr downgrade
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Downgrading to older release (should be none)"
fwupdmgr downgrade
rc=$?; if [[ $rc != 2 ]]; then exit $rc; fi

# ---
echo "Updating all devices to latest release (requires network access)"
fwupdmgr update
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Getting updates (should be none)..."
fwupdmgr get-updates
rc=$?; if [[ $rc != 2 ]]; then exit $rc; fi

# ---
echo "Refreshing from the LVFS (requires network access)..."
fwupdmgr refresh
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# ---
echo "Flashing actual devices (requires specific hardware)"
${dirname}/hardware.py
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

# success!
exit 0
