#!/bin/sh

export NO_COLOR=1

exec 0>/dev/null
exec 2>&1
device=08d460be0f1f9f128413f816022a6439e0078018

CAB="@installedtestsdir@/fakedevice123.cab"

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
    expect_rc 0

    # ---
    echo "Examining ${CAB} (json)..."
    fwupdmgr get-details ${CAB} --json
    expect_rc 0

    # ---
    echo "Installing ${CAB} cabinet..."
    fwupdmgr install ${CAB} --no-reboot-check
    expect_rc 0
fi

# ---
echo "Getting the list of remotes..."
fwupdmgr get-remotes
expect_rc 0

# ---
echo "Getting the list of remotes (json)..."
fwupdmgr get-remotes --json
expect_rc 0

# ---
echo "Modifying config..."
fwupdmgr modify-config fwupd UpdateMotd false --json
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

# ---
echo "Run security tests..."
fwupdmgr security

# ---
echo "Run security tests (json)..."
fwupdmgr security --json

# ---
echo "Verify test device is present"
fwupdmgr get-devices --json | jq -e '.Devices | any(.Plugin == "test")'
if [ $? != 0 ]; then
    echo "Skipping tests due to no test device enabled"
    exit 0
fi

# ---
echo "Resetting config..."
fwupdmgr reset-config test
expect_rc 0

# ---
echo "Update the device hash database..."
fwupdmgr get-releases $device
expect_rc 0

# ---
echo "Disabling vendor-directory remote..."
fwupdmgr disable-remote vendor-directory
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

# success!
exit 0
