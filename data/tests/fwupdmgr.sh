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

run() {
    cmd="@bindir@/fwupdmgr $*"
    if [ -n "$SNAP" ]; then
        cmd="$SNAP/$cmd"
    fi
    $cmd
}

# ---
echo " ● Show help output"
run --help
expect_rc 0

# ---
echo " ● Show version output"
run --version
expect_rc 0

# ---
echo " ● Getting the list of plugins…"
run get-plugins
expect_rc 0

if [ -n "$CI" ]; then
    # ---
    echo " ● Setting BIOS setting…"
    run set-bios-setting fwupd_self_test value
    expect_rc 0

    # ---
    echo " ● Getting BIOS settings…"
    run get-bios-setting
    expect_rc 0

    # ---
    echo " ● Getting BIOS settings (JSON)…"
    run get-bios-setting --json
    expect_rc 0

    # ---
    echo " ● Getting BIOS settings (unfound)…"
    run get-bios-setting foo
    expect_rc 3

    # ---
    echo " ● Setting BIOS setting (unfound)…"
    run set-bios-setting unfound value
    expect_rc 3
fi

# ---
echo " ● Getting the list of plugins (JSON)…"
run get-plugins --json
expect_rc 0

if [ -f ${CAB} ]; then
    # ---
    echo " ● Examining ${CAB}…"
    run get-details ${CAB}
    expect_rc 0

    # ---
    echo " ● Examining ${CAB} (JSON)…"
    run get-details ${CAB} --json
    expect_rc 0

    # ---
    echo " ● Installing ${CAB} cabinet…"
    run install ${CAB} --no-reboot-check --allow-reinstall --allow-older
    expect_rc 0
fi

# ---
echo " ● Getting the list of remotes…"
run get-remotes
expect_rc 0

# ---
echo " ● Getting the list of remotes (JSON)…"
run get-remotes --json
expect_rc 0

# ---
echo " ● Modifying config…"
run modify-config fwupd UpdateMotd false --json
expect_rc 0

# ---
echo " ● Inhibiting for 100ms…"
run inhibit test 100
expect_rc 0

# ---
echo " ● Uninhibiting for invalid ID…"
run uninhibit test
expect_rc 3

# ---
echo " ● Add blocked firmware…"
run block-firmware foo
expect_rc 0

# ---
echo " ● Add blocked firmware (again)…"
run block-firmware foo
expect_rc 2

# ---
echo " ● Getting blocked firmware…"
run get-blocked-firmware
expect_rc 0

# ---
echo " ● Remove blocked firmware…"
run unblock-firmware foo
expect_rc 0

# ---
echo " ● Remove blocked firmware (again)…"
run unblock-firmware foo
expect_rc 2

# ---
echo " ● Setting approved firmware…"
run set-approved-firmware foo,bar,baz
expect_rc 0

# ---
echo " ● Getting approved firmware…"
run get-approved-firmware
expect_rc 0

# ---
echo " ● Getting approved firmware (JSON)…"
run get-approved-firmware --json
expect_rc 0

# ---
echo " ● Run security tests…"
run security

# ---
echo " ● Run security tests (JSON)…"
run security --json

# ---
echo " ● Get HWIDs…"
run hwids
expect_rc 0

# ---
echo " ● Get HWIDs (JSON)…"
run hwids --json
expect_rc 0

# ---
echo " ● Verify test device is present"
run get-devices --json | jq -e '.Devices | any(.Plugin == "test")'
if [ $? != 0 ]; then
    echo " ● Skipping tests due to no test device enabled"
    exit 0
fi

# ---
echo " ● Resetting config…"
run reset-config test
expect_rc 0

# ---
echo " ● Get releases…"
run get-releases ${DEVICE}
expect_rc 0

# ---
echo " ● Get releases (JSON)…"
run get-releases ${DEVICE} --json
expect_rc 0

# ---
echo " ● Disabling vendor-directory remote…"
run disable-remote vendor-directory
expect_rc 0

# ---
echo " ● Enable vendor-directory remote…"
run enable-remote vendor-directory
expect_rc 0

# ---
echo " ● Modify vendor-directory remote…"
run modify-remote vendor-directory Enabled true
expect_rc 0

# ---
echo " ● Update the device hash database…"
run verify-update ${DEVICE}
expect_rc 0

# ---
echo " ● Getting devices (should be one)…"
run get-devices --no-unreported-check
expect_rc 0

# ---
echo " ● Getting one device (should be one)…"
run get-devices ${DEVICE} --no-unreported-check
expect_rc 0

# ---
echo " ● Getting filtered devices (should be none)…"
run get-devices --no-unreported-check --filter wait-for-replug --filter-release trusted-payload -vv
expect_rc 2

# ---
echo " ● Testing the verification of firmware…"
run verify ${DEVICE}
expect_rc 0

# ---
echo " ● Unlocking device (not needed)…"
run unlock ${DEVICE}
expect_rc 1

# ---
echo " ● Testing waiting for device…"
run device-wait b585990a-003e-5270-89d5-3705a17f9a43
expect_rc 0

# success!
exit 0
