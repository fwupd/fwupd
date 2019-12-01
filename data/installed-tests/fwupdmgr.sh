#!/bin/bash

exec 2>&1
dirname=`dirname $0`
device=08d460be0f1f9f128413f816022a6439e0078018

error()
{
        rc=$1
        journalctl -u fwupd -b || true
        exit $rc
}

# ---
echo "Getting the list of remotes..."
fwupdmgr get-remotes
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Enabling fwupd-tests remote..."
fwupdmgr enable-remote fwupd-tests
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Update the device hash database..."
fwupdmgr verify-update $device
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Getting devices (should be one)..."
fwupdmgr get-devices --no-unreported-check
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Testing the verification of firmware..."
fwupdmgr verify $device
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Getting updates (should be one)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Installing test firmware..."
fwupdmgr install ${dirname}/fakedevice124.cab
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Getting updates (should be none)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
rc=$?; if [[ $rc != 2 ]]; then error $rc; fi

# ---
echo "Testing the verification of firmware (again)..."
fwupdmgr verify $device
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Downgrading to older release (requires network access)"
fwupdmgr downgrade $device
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Downgrading to older release (should be none)"
fwupdmgr downgrade $device
rc=$?; if [[ $rc != 2 ]]; then error $rc; fi

# ---
echo "Updating all devices to latest release (requires network access)"
fwupdmgr --no-unreported-check --no-metadata-check --no-reboot-check update
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Getting updates (should be none)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
rc=$?; if [[ $rc != 2 ]]; then error $rc; fi

# ---
echo "Refreshing from the LVFS (requires network access)..."
fwupdmgr refresh
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# ---
echo "Flashing actual devices (requires specific hardware)"
${dirname}/hardware.py
rc=$?; if [[ $rc != 0 ]]; then error $rc; fi

# success!
exit 0
