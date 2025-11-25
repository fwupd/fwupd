#!/bin/sh

export NO_COLOR=1

exec 0>/dev/null
exec 2>&1

DEVICE=08d460be0f1f9f128413f816022a6439e0078018
CAB="@installedtestsdir@/fakedevice123.cab"

error() {
    if [ -f "fwupd.txt" ]; then
        cat fwupd.txt
    else
        journalctl -u fwupd -b || true
    fi
    echo " ● Exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

# ---
echo " ● Show help output"
fwupdmgr --help
expect_rc 0

# ---
echo " ● Show version output"
fwupdmgr --version
expect_rc 0

# ---
echo " ● Getting the list of plugins…"
fwupdmgr get-plugins
expect_rc 0

if [ -n "$CI" ]; then
    # ---
    echo " ● Setting BIOS setting…"
    fwupdmgr set-bios-setting fwupd_self_test value
    expect_rc 0

    # ---
    echo " ● Getting BIOS settings…"
    fwupdmgr get-bios-setting
    expect_rc 0

    # ---
    echo " ● Getting BIOS settings (JSON)…"
    fwupdmgr get-bios-setting --json
    expect_rc 0

    # ---
    echo " ● Getting BIOS settings (unfound)…"
    fwupdmgr get-bios-setting foo
    expect_rc 3

    # ---
    echo " ● Setting BIOS setting (unfound)…"
    fwupdmgr set-bios-setting unfound value
    expect_rc 3
fi

# ---
echo " ● Getting the list of plugins (JSON)…"
fwupdmgr get-plugins --json
expect_rc 0

if [ -f ${CAB} ]; then
    # ---
    echo " ● Examining ${CAB}…"
    fwupdmgr get-details ${CAB}
    expect_rc 0

    # ---
    echo " ● Examining ${CAB} (JSON)…"
    fwupdmgr get-details ${CAB} --json
    expect_rc 0

    # ---
    echo " ● Installing ${CAB} cabinet…"
    fwupdmgr install ${CAB} --no-reboot-check --allow-reinstall --allow-older
    expect_rc 0
fi

# ---
echo " ● Getting the list of remotes…"
fwupdmgr get-remotes
expect_rc 0

# ---
echo " ● Getting the list of remotes (JSON)…"
fwupdmgr get-remotes --json
expect_rc 0

# ---
echo " ● Modifying config…"
fwupdmgr modify-config fwupd UpdateMotd false --json
expect_rc 0

# ---
echo " ● Inhibiting for 100ms…"
fwupdmgr inhibit test 100
expect_rc 0

# ---
echo " ● Uninhibiting for invalid ID…"
fwupdmgr uninhibit test
expect_rc 3

# ---
echo " ● Add blocked firmware…"
fwupdmgr block-firmware foo
expect_rc 0

# ---
echo " ● Add blocked firmware (again)…"
fwupdmgr block-firmware foo
expect_rc 2

# ---
echo " ● Getting blocked firmware…"
fwupdmgr get-blocked-firmware
expect_rc 0

# ---
echo " ● Remove blocked firmware…"
fwupdmgr unblock-firmware foo
expect_rc 0

# ---
echo " ● Remove blocked firmware (again)…"
fwupdmgr unblock-firmware foo
expect_rc 2

# ---
echo " ● Setting approved firmware…"
fwupdmgr set-approved-firmware foo,bar,baz
expect_rc 0

# ---
echo " ● Getting approved firmware…"
fwupdmgr get-approved-firmware
expect_rc 0

# ---
echo " ● Getting approved firmware (JSON)…"
fwupdmgr get-approved-firmware --json
expect_rc 0

# ---
echo " ● Run security tests…"
fwupdmgr security

# ---
echo " ● Run security tests (JSON)…"
fwupdmgr security --json

# ---
echo " ● Get HWIDs…"
fwupdmgr hwids
expect_rc 0

# ---
echo " ● Get HWIDs (JSON)…"
fwupdmgr hwids --json
expect_rc 0

# ---
echo " ● Verify test device is present"
fwupdmgr get-devices --json | jq -e '.Devices | any(.Plugin == "test")'
if [ $? != 0 ]; then
    echo " ● Skipping tests due to no test device enabled"
    exit 0
fi

# ---
echo " ● Resetting config…"
fwupdmgr reset-config test
expect_rc 0

# ---
echo " ● Get releases…"
fwupdmgr get-releases ${DEVICE}
expect_rc 0

# ---
echo " ● Get releases (JSON)…"
fwupdmgr get-releases ${DEVICE} --json
expect_rc 0

# ---
echo " ● Disabling vendor-directory remote…"
fwupdmgr disable-remote vendor-directory
expect_rc 0

# ---
echo " ● Enable vendor-directory remote…"
fwupdmgr enable-remote vendor-directory
expect_rc 0

# ---
echo " ● Modify vendor-directory remote…"
fwupdmgr modify-remote vendor-directory Enabled true
expect_rc 0

# ---
echo " ● Update the device hash database…"
fwupdmgr verify-update ${DEVICE}
expect_rc 0

# ---
echo " ● Getting devices (should be one)…"
fwupdmgr get-devices --no-unreported-check
expect_rc 0

# ---
echo " ● Getting one device (should be one)…"
fwupdmgr get-devices ${DEVICE} --no-unreported-check
expect_rc 0

# ---
echo " ● Getting filtered devices (should be none)…"
fwupdmgr get-devices --no-unreported-check --filter wait-for-replug --filter-release trusted-payload -vv
expect_rc 2

# ---
echo " ● Testing the verification of firmware…"
fwupdmgr verify ${DEVICE}
expect_rc 0

# ---
echo " ● Unlocking device (not needed)…"
fwupdmgr unlock ${DEVICE}
expect_rc 1

# ---
echo " ● Testing waiting for device…"
fwupdmgr device-wait b585990a-003e-5270-89d5-3705a17f9a43
expect_rc 0

# success!
exit 0
