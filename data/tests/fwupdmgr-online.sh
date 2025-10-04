#!/bin/sh

export NO_COLOR=1

exec 0>/dev/null
exec 2>&1
device=08d460be0f1f9f128413f816022a6439e0078018

error() {
    if [ -f "fwupd.txt" ]; then
        cat fwupd.txt
    else
        journalctl -u fwupd -b || true
    fi
    echo "exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

if [ -z "$CI_NETWORK" ]; then
    echo "Skipping network tests due to CI_NETWORK not being set"
    exit 0
fi

# ---
echo "Verify test device is present"
fwupdtool get-devices --json | jq -e '.Devices | any(.Plugin == "test")'
rc=$?
if [ $rc != 0 ]; then
    echo "Enable test device"
    fwupdtool enable-test-devices
    expect_rc 0
fi

# ---
echo "Getting devices (should be one)..."
fwupdmgr get-devices --no-unreported-check
expect_rc 0

# ---
echo "Refreshing from the LVFS..."
fwupdmgr --download-retries=5 refresh --force
expect_rc 0

# ---
echo "Showing remote ages..."
fwupdmgr get-remotes lvfs
expect_rc 0

# ---
echo "Refreshing from the LVFS (already up to date)..."
fwupdmgr --download-retries=5 refresh
expect_rc 2

# ---
echo "Getting updates (should be one)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 0

# ---
echo "Installing test firmware..."
fwupdmgr update $device -y
expect_rc 0

# ---
echo "Getting history (should be one)..."
fwupdmgr get-history
expect_rc 0

# ---
echo "Check if anything was tagged for emulation"
fwupdmgr get-devices --json --filter emulation-tag | jq -e '(.Devices | length) > 0'
rc=$?
if [ $rc = 0 ]; then
    echo "Save device emulation"
    fwupdmgr emulation-save /dev/null
    expect_rc 0
    echo "Save device emulation (bad args)"
    fwupdmgr emulation-save
    expect_rc 1
fi

# ---
echo "Verifying results (str)..."
fwupdmgr get-results $device -y
expect_rc 0

# ---
echo "Verifying results (json)..."
fwupdmgr get-results $device -y --json
expect_rc 0

# ---
echo "Getting updates (should be none)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 2

# ---
echo "Getting updates [json] (should be none)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates --json
expect_rc 0

# ---
echo "Testing the verification of firmware (again)..."
fwupdmgr verify $device
expect_rc 0

# ---
echo "Downgrading to older release"
fwupdmgr --download-retries=5 downgrade $device -y
expect_rc 0

# ---
echo "Downgrading to older release (should be none)"
fwupdmgr downgrade $device
expect_rc 2

# ---
echo "Updating all devices to latest release"
fwupdmgr --download-retries=5 --no-unreported-check --no-metadata-check --no-reboot-check update -y
expect_rc 0

# ---
echo "Getting updates (should be none)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 2

# ---
echo "Check reboot behavior"
fwupdmgr quit
fwupdtool modify-config test NeedsReboot true
expect_rc 0
fwupdmgr update $device -y
expect_rc 0
fwupdmgr check-reboot-needed $device --json
expect_rc 0
fwupdtool modify-config test NeedsReboot false

# ---
echo "Resetting config..."
fwupdmgr reset-config test
expect_rc 0

# check we can search for known tokens
fwupdmgr search CVE-2022-21894
expect_rc 0

# check we do not find a random search result
fwupdmgr search DOESNOTEXIST
expect_rc 3

# success!
exit 0
