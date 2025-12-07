#!/bin/sh

export NO_COLOR=1

exec 0>/dev/null
exec 2>&1

TMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR"' EXIT

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

run() {
    if [ ! -x @bindir@/fwupdmgr ]; then
        cmd="@bindir@/fwupdmgr $*"
    else
        # for the snap CI target
        cmd="fwupdmgr $*"
    fi
    $cmd
}

if [ -z "$CI_NETWORK" ]; then
    echo " ● Skipping network tests due to CI_NETWORK not being set"
    exit 0
fi

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
echo " ● Downloading random file…"
run download https://cdn.fwupd.org/static/img/user-solid.svg --force
expect_rc 0

# ---
echo " ● Getting devices (should be one)…"
run get-devices --no-unreported-check
expect_rc 0

# ---
echo " ● Clean remote"
run clean-remote lvfs
expect_rc 0

# ---
echo " ● Showing remote ages (JSON)…"
run get-remotes lvfs --json
expect_rc 0

# ---
echo " ● Refreshing from the LVFS…"
run --download-retries=5 refresh --force
expect_rc 0
ls -R @localstatedir@/lib/fwupd/metadata/lvfs/firmware.xml.zst*
expect_rc 0

# ---
echo " ● Showing remote ages…"
run get-remotes lvfs
expect_rc 0

# ---
echo " ● Refreshing (already up to date)…"
cp @localstatedir@/lib/fwupd/metadata/lvfs/firmware.xml.zst* ${TMPDIR}
expect_rc 0
run refresh ${TMPDIR}/firmware.xml.zst ${TMPDIR}/firmware.xml.zst.jcat lvfs
expect_rc 2

# ---
echo " ● Sync BKC…"
run sync
expect_rc 2

# ---
echo " ● Check we can search for known tokens…"
run search CVE-2022-21894
expect_rc 0

# ---
echo " ● Check we do not find a random search result…"
run search DOESNOTEXIST
expect_rc 3

# ---
echo " ● Install a specific release…"
run --no-unreported-check --no-metadata-check --allow-reinstall --allow-older install ${DEVICE} 1.2.3
expect_rc 0

# ---
echo " ● Getting updates (should be one)…"
run --no-unreported-check --no-metadata-check get-updates
expect_rc 0

# ---
echo " ● Getting updates (should still be one)…"
run --no-unreported-check --no-metadata-check get-updates ${DEVICE}
expect_rc 0

# ---
echo " ● Installing test firmware…"
run update ${DEVICE} -y
expect_rc 0

# ---
echo " ● Update when not needed…"
run update ${DEVICE} -y
expect_rc 0

# ---
echo " ● Reinstall current release…"
run reinstall ${DEVICE} -y
expect_rc 0

# ---
echo " ● Switch branch of device (impossible)…"
run switch-branch ${DEVICE} impossible
expect_rc 1

# ---
echo " ● Activate device (not required)…"
run activate ${DEVICE}
expect_rc 2

# ---
echo " ● Getting history (should be one)…"
run get-history
expect_rc 0

# ---
echo " ● Getting history (JSON)…"
run get-history --json
expect_rc 0

# ---
echo " ● Exporting history…"
run report-export
expect_rc 0

# ---
echo " ● Check if anything was tagged for emulation"
run get-devices --json --filter emulation-tag | jq -e '(.Devices | length) > 0'
rc=$?
if [ $rc = 0 ]; then
    echo " ● Save device emulation"
    run emulation-save /dev/null
    expect_rc 0
    echo " ● Save device emulation (bad args)"
    run emulation-save
    expect_rc 1
fi

# ---
echo " ● Verifying results (str)…"
run get-results ${DEVICE} -y
expect_rc 0

# ---
echo " ● Verifying results (JSON)…"
run get-results ${DEVICE} -y --json
expect_rc 0

# ---
echo " ● Getting updates (should be none)…"
run --no-unreported-check --no-metadata-check get-updates
expect_rc 2

# ---
echo " ● Getting updates (JSON) (should be none)…"
run --no-unreported-check --no-metadata-check get-updates --json
expect_rc 0

# ---
echo " ● Updating verification…"
run verify-update ${DEVICE}
expect_rc 0

# ---
echo " ● Testing verification…"
run verify ${DEVICE}
expect_rc 0

# ---
echo " ● Downgrading to older release"
run --download-retries=5 downgrade ${DEVICE} -y
expect_rc 0

# ---
echo " ● Downgrading to older release (should be none)"
run downgrade ${DEVICE}
expect_rc 2

# ---
echo " ● Updating all devices to latest release"
run --download-retries=5 --no-unreported-check --no-metadata-check --no-reboot-check update -y
expect_rc 0

# ---
echo " ● Getting updates (should be none)…"
run --no-unreported-check --no-metadata-check get-updates
expect_rc 2

# ---
echo " ● Clearing results…"
run clear-results --json ${DEVICE}
expect_rc 0

# ---
echo " ● Check reboot behavior"
run modify-config test NeedsReboot true
expect_rc 0
run --no-unreported-check --no-metadata-check --allow-reinstall --allow-older install ${DEVICE} 1.2.3
expect_rc 0
run check-reboot-needed ${DEVICE} --json
expect_rc 0

# ---
echo " ● Resetting config…"
run reset-config test
expect_rc 0

# success!
exit 0
