#!/bin/sh

export NO_COLOR=1

exec 0>/dev/null
exec 2>&1

DEVICE=08d460be0f1f9f128413f816022a6439e0078018

error() {
    if [ -f "fwupd.txt" ]; then
        cat fwupd.txt
    else
        journalctl -u fwupd -b || true
    fi
    echo " ● exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

if [ -z "$CI_NETWORK" ]; then
    echo " ● Skipping network tests due to CI_NETWORK not being set"
    exit 0
fi

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
echo " ● Downloading random file…"
fwupdmgr download https://cdn.fwupd.org/static/img/user-solid.svg --force
expect_rc 0

# ---
echo " ● Getting devices (should be one)…"
fwupdmgr get-devices --no-unreported-check
expect_rc 0

# ---
echo " ● Refreshing from the LVFS…"
fwupdmgr --download-retries=5 refresh --force
expect_rc 0

# ---
echo " ● Showing remote ages…"
fwupdmgr get-remotes lvfs
expect_rc 0

# ---
echo " ● Refreshing from the LVFS (already up to date)…"
fwupdmgr --download-retries=5 refresh
expect_rc 2

# ---
echo " ● Sync BKC…"
fwupdmgr sync
expect_rc 2

# ---
echo " ● Check we can search for known tokens…"
fwupdmgr search CVE-2022-21894
expect_rc 0

# ---
echo " ● Check we do not find a random search result…"
fwupdmgr search DOESNOTEXIST
expect_rc 3

# ---
echo " ● Install a specific release…"
fwupdmgr --no-unreported-check --no-metadata-check --allow-reinstall --allow-older install ${DEVICE} 1.2.3
expect_rc 0

# ---
echo " ● Getting updates (should be one)…"
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 0

# ---
echo " ● Getting updates (should still be one)…"
fwupdmgr --no-unreported-check --no-metadata-check get-updates ${DEVICE}
expect_rc 0

# ---
echo " ● Installing test firmware…"
fwupdmgr update ${DEVICE} -y
expect_rc 0

# ---
echo " ● Update when not needed…"
fwupdmgr update ${DEVICE} -y
expect_rc 0

# ---
echo " ● Reinstall current release…"
fwupdmgr reinstall ${DEVICE} -y
expect_rc 0

# ---
echo " ● Switch branch of device (impossible)…"
fwupdmgr switch-branch ${DEVICE} impossible
expect_rc 1

# ---
echo " ● Activate device (not required)…"
fwupdmgr activate ${DEVICE}
expect_rc 2

# ---
echo " ● Getting history (should be one)…"
fwupdmgr get-history
expect_rc 0

# ---
echo " ● Getting history (JSON)…"
fwupdmgr get-history --json
expect_rc 0

# ---
echo " ● Exporting history…"
fwupdmgr report-export
expect_rc 0

# ---
echo " ● Check if anything was tagged for emulation"
fwupdmgr get-devices --json --filter emulation-tag | jq -e '(.Devices | length) > 0'
rc=$?
if [ $rc = 0 ]; then
    echo " ● Save device emulation"
    fwupdmgr emulation-save /dev/null
    expect_rc 0
    echo " ● Save device emulation (bad args)"
    fwupdmgr emulation-save
    expect_rc 1
fi

# ---
echo " ● Verifying results (str)…"
fwupdmgr get-results ${DEVICE} -y
expect_rc 0

# ---
echo " ● Verifying results (JSON)…"
fwupdmgr get-results ${DEVICE} -y --json
expect_rc 0

# ---
echo " ● Getting updates (should be none)…"
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 2

# ---
echo " ● Getting updates (JSON) (should be none)…"
fwupdmgr --no-unreported-check --no-metadata-check get-updates --json
expect_rc 0

# ---
echo " ● Updating verification…"
fwupdmgr verify-update ${DEVICE}
expect_rc 0

# ---
echo " ● Testing verification…"
fwupdmgr verify ${DEVICE}
expect_rc 0

# ---
echo " ● Downgrading to older release"
fwupdmgr --download-retries=5 downgrade ${DEVICE} -y
expect_rc 0

# ---
echo " ● Downgrading to older release (should be none)"
fwupdmgr downgrade ${DEVICE}
expect_rc 2

# ---
echo " ● Updating all devices to latest release"
fwupdmgr --download-retries=5 --no-unreported-check --no-metadata-check --no-reboot-check update -y
expect_rc 0

# ---
echo " ● Getting updates (should be none)…"
fwupdmgr --no-unreported-check --no-metadata-check get-updates
expect_rc 2

# ---
echo " ● Clearing results…"
fwupdmgr clear-results --json ${DEVICE}
expect_rc 0

# ---
echo " ● Check reboot behavior"
fwupdmgr modify-config test NeedsReboot true
expect_rc 0
fwupdmgr --no-unreported-check --no-metadata-check --allow-reinstall --allow-older install ${DEVICE} 1.2.3
expect_rc 0
fwupdmgr check-reboot-needed ${DEVICE} --json
expect_rc 0

# ---
echo " ● Resetting config…"
fwupdmgr reset-config test
expect_rc 0

# success!
exit 0
