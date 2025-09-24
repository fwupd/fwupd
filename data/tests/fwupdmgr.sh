#!/bin/sh

export NO_COLOR=1

exec 0>/dev/null
exec 2>&1
device=08d460be0f1f9f128413f816022a6439e0078018

CAB="@installedtestsdir@/fakedevice123.cab"

error()
{
    rc=$1
    if [ -f "fwupd.txt" ]; then
        cat fwupd.txt
    else
        journalctl -u fwupd -b || true
    fi
    exit $rc
}

expect_rc() {
    expected=$1
    rc=$?

    [ "$expected" -eq "$rc" ] || error "$rc"
}

# ---
echo "Verify test device is present"
fwupdtool get-devices --json | jq -e '.Devices | any(.Plugin == "test")'
rc=$?; if [ $rc != 0 ]; then
    echo "Enable test device"
    fwupdtool enable-test-devices
    expect_rc 0
fi

# ---
echo "Show help output"
fwupdmgr --help
expect_rc 0

# ---
echo "Show version output"
fwupdmgr --version
expect_rc 0

# ---
echo "Getting the list of plugins..."
fwupdmgr get-plugins
expect_rc 0

if [ -n "$CI" ]; then
    # ---
    echo "Setting BIOS setting..."
    fwupdmgr set-bios-setting fwupd_self_test value
    expect_rc 0

    # ---
    echo "Getting BIOS settings..."
    fwupdmgr get-bios-setting
    expect_rc 0

    # ---
    echo "Getting BIOS settings (json)..."
    fwupdmgr get-bios-setting --json
    expect_rc 0

    # ---
    echo "Getting BIOS settings (unfound)..."
    fwupdmgr get-bios-setting foo
    expect_rc 3

    # ---
    echo "Setting BIOS setting (unfound)..."
    fwupdmgr set-bios-setting unfound value
    expect_rc 3
fi

# ---
echo "Getting the list of plugins (json)..."
fwupdmgr get-plugins --json
expect_rc 0

if [ -f ${CAB} ]; then
    # ---
    echo "Examining ${CAB}..."
    fwupdmgr get-details ${CAB}
    rc=$?; if [ $rc != 0 ]; then exit $rc; fi

    # ---
    echo "Examining ${CAB} (json)..."
    fwupdmgr get-details ${CAB} --json
    rc=$?; if [ $rc != 0 ]; then exit $rc; fi

    # ---
    echo "Installing ${CAB} cabinet..."
    fwupdmgr install ${CAB} --no-reboot-check
    rc=$?; if [ $rc != 0 ]; then exit $rc; fi
fi

# ---
echo "Update the device hash database..."
fwupdmgr get-releases $device
expect_rc 0

# ---
echo "Getting the list of remotes..."
fwupdmgr get-remotes
expect_rc 0

# ---
echo "Disabling vendor-directory remote..."
fwupdmgr disable-remote vendor-directory
expect_rc 0

# ---
echo "Getting the list of remotes (json)..."
fwupdmgr get-remotes --json
expect_rc 0

# ---
echo "Enable vendor-directory remote..."
fwupdmgr enable-remote vendor-directory
expect_rc 0

# ---
echo "Update the device hash database..."
fwupdmgr verify-update $device
expect_rc 0

# ---
echo "Getting devices (should be one)..."
fwupdmgr get-devices --no-unreported-check
expect_rc 0

# ---
echo "Testing the verification of firmware..."
fwupdmgr verify $device
expect_rc 0

# ---
echo "Getting updates (should be one)..."
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 0

# ---
echo "Installing test firmware..."
fwupdmgr update $device -y
expect_rc 0

# ---
echo "Check if anything was tagged for emulation"
fwupdmgr get-devices --json --filter emulation-tag | jq -e '(.Devices | length) > 0'
rc=$?; if [ $rc = 0 ]; then
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
echo "Getting history (should be none)..."
fwupdmgr get-history
rc=$?; if [ $rc != 2 ]; then exit $rc; fi

if [ -n "$CI_NETWORK" ]; then
    # ---
    echo "Downgrading to older release (requires network access)"
    fwupdmgr --download-retries=5 downgrade $device -y
    expect_rc 0

    # ---
    echo "Downgrading to older release (should be none)"
    fwupdmgr downgrade $device
    expect_rc 2

    # ---
    echo "Updating all devices to latest release (requires network access)"
    fwupdmgr --download-retries=5 --no-unreported-check --no-metadata-check --no-reboot-check update -y
    expect_rc 0

    # ---
    echo "Getting updates (should be none)..."
    fwupdmgr --no-unreported-check --no-metadata-check get-updates
    expect_rc 2

    # ---
    echo "Refreshing from the LVFS (requires network access)..."
    fwupdmgr --download-retries=5 refresh
    expect_rc 0

    # check we can search for known tokens
    fwupdmgr search CVE-2022-21894
    expect_rc 0

    # check we do not find a random search result
    fwupdmgr search DOESNOTEXIST
    expect_rc 3
else
        echo "Skipping network tests due to CI_NETWORK not being set"
fi

# ---
echo "Modifying config..."
fwupdmgr modify-config fwupd UpdateMotd false --json
expect_rc 0

# ---
echo "Resetting changed config..."
fwupdmgr reset-config fwupd --json
expect_rc 0

# ---
echo "Resetting empty config ..."
fwupdmgr reset-config fwupd --json
expect_rc 0

# ---
echo "Inhibiting for 100ms..."
fwupdmgr inhibit test 100
expect_rc 0

# ---
echo "Add blocked firmware..."
fwupdmgr block-firmware foo
expect_rc 0

# ---
echo "Add blocked firmware (again)..."
fwupdmgr block-firmware foo
expect_rc 2

# ---
echo "Getting blocked firmware..."
fwupdmgr get-blocked-firmware
expect_rc 0

# ---
echo "Remove blocked firmware..."
fwupdmgr unblock-firmware foo
expect_rc 0

# ---
echo "Remove blocked firmware (again)..."
fwupdmgr unblock-firmware foo
expect_rc 2

# ---
echo "Setting approved firmware..."
fwupdmgr set-approved-firmware foo,bar,baz
expect_rc 0

# ---
echo "Getting approved firmware..."
fwupdmgr get-approved-firmware
expect_rc 0

UNAME=$(uname -m)
if [ "${UNAME}" = "x86_64" ] || [ "${UNAME}" = "x86" ]; then
       EXPECTED=0
else
       EXPECTED=1
fi
# ---
echo "Run security tests..."
fwupdmgr security
expect_rc $EXPECTED

# ---
echo "Run security tests (json)..."
fwupdmgr security --json
expect_rc $EXPECTED

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

# success!
exit 0
