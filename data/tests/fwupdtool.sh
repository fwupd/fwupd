#!/bin/sh

exec 0>/dev/null
exec 2>&1

export NO_COLOR=1
export FWUPD_VERBOSE=1
CAB=fakedevice124.cab
INPUT="@installedtestsdir@/fakedevice124.bin \
       @installedtestsdir@/fakedevice124.jcat \
       @installedtestsdir@/fakedevice124.metainfo.xml"
DEVICE=08d460be0f1f9f128413f816022a6439e0078018

error() {
    rc=$1
    cat fwupdtool.txt
    exit $rc
}

expect_rc() {
    expected=$1
    rc=$?

    [ "$expected" -eq "$rc" ] || error "$rc"
}

run() {
    cmd="fwupdtool -v $*"
    echo "cmd: $cmd" >fwupdtool.txt
    $cmd 1>>fwupdtool.txt 2>&1
}

# ---
echo "Show help output"
run --help
expect_rc 0

# ---
echo "Show version output"
run --version
expect_rc 0

# ---
echo "Show version output (json)"
run --version --json
expect_rc 0

# ---
echo "Showing hwids"
run hwids
expect_rc 0

UNAME=$(uname -m)
if [ "${UNAME}" = "x86_64" ] || [ "${UNAME}" = "x86" ]; then
    EXPECTED=0
else
    EXPECTED=1
fi
# ---
echo "Showing security"
run security
expect_rc $EXPECTED

# ---
echo "Showing plugins"
run get-plugins
expect_rc 0

# ---
echo "Showing plugins (json)"
run get-plugins --json
expect_rc 0

# ---
echo "Enabling test device..."
run enable-test-devices
expect_rc 0

# ---
echo "Checking device-flags"
run get-device-flags
expect_rc 0

# ---
echo "Checking firmware-gtypes"
run get-firmware-gtypes
expect_rc 0

# ---
echo "Checking firmware-types"
run get-firmware-types
expect_rc 0

# ---
echo "Checking for updates"
run get-updates
expect_rc 0

# ---
echo "Checking for updates"
run get-updates --json
expect_rc 0

# ---
echo "Building ${CAB}..."
run build-cabinet ${CAB} ${INPUT} --force
expect_rc 0

# ---
echo "Examining ${CAB}..."
run get-details ${CAB}
expect_rc 0

# ---
echo "Installing ${CAB} cabinet..."
run install ${CAB}
expect_rc 0

# ---
echo "Cleaning ${CAB} generated cabinet ..."
rm -f ${CAB}

# ---
echo "Verifying update..."
run verify-update ${DEVICE}
expect_rc 0

# ---
echo "Getting history (should be one)..."
run get-history
expect_rc 0

# ---
echo "Clearing history..."
run clear-history ${DEVICE}
expect_rc 0

# ---
echo "Getting history (should be none)..."
run get-history
expect_rc 2

# ---
echo "Resetting config..."
run reset-config test
expect_rc 0

# ---
echo "Testing good version compare"
run vercmp 1.0.0 1.0.0 triplet
expect_rc 0

# ---
echo "Testing bad version compare"
run vercmp 1.0.0 1.0.1 foo
expect_rc 1

# ---
echo "Getting supported version formats..."
run get-version-formats --json
expect_rc 0

# ---
echo "Getting report metadata..."
run get-report-metadata --json
expect_rc 0

# ---
echo "Getting the list of remotes"
run get-remotes --json
expect_rc 0

# ---
echo "Disabling LVFS remote..."
run modify-remote lvfs Enabled false
expect_rc 0

# ---
echo "Enabling LVFS remote..."
run modify-remote lvfs Enabled true
expect_rc 0

# ---
echo "Modify unknown remote (should fail)..."
run modify-remote foo Enabled true
expect_rc 1

# ---
echo "Modify known remote but unknown key (should fail)..."
run modify-remote lvfs bar true
expect_rc 3

# ---
echo "Getting devices (should be one)..."
run get-devices --json
expect_rc 0

# ---
echo "Getting all devices, even unsupported ones..."
run get-devices --show-all --force
expect_rc 0

# ---
echo "Changing VALID config on test device..."
run modify-config test AnotherWriteRequired true
expect_rc 0

# ---
echo "Changing INVALID config on test device...(should fail)"
run modify-config test Foo true
expect_rc 1

# ---
echo "Disabling test device..."
run disable-test-devices
expect_rc 0

BASEDIR=@installedtestsdir@/tests/bios-attrs/dell-xps13-9310/
if [ -d $BASEDIR ]; then
    WORKDIR="$(mktemp -d)"
    cp $BASEDIR $WORKDIR -r
    export FWUPD_SYSFSFWATTRIBDIR=$WORKDIR/dell-xps13-9310/
    # ---
    echo "Get BIOS settings..."
    run get-bios-settings --json
    expect_rc 0

    # ---
    echo "Get BIOS setting as json..."
    run get-bios-settings WlanAutoSense --json
    expect_rc 0

    # ---
    echo "Get BIOS setting as a string..."
    run get-bios-settings WlanAutoSense
    expect_rc 0

    # ---
    echo "Modify BIOS setting to different value..."
    run set-bios-setting WlanAutoSense Enabled --no-reboot-check
    expect_rc 0

    # ---
    echo "Modify BIOS setting back to default..."
    run set-bios-setting WlanAutoSense Disabled --no-reboot-check
    expect_rc 0

    # ---
    echo "Modify BIOS setting to bad value (should fail)..."
    run set-bios-setting WlanAutoSense foo --no-reboot-check
    expect_rc 1

    # ---
    echo "Modify Unknown BIOS setting (should fail)..."
    run set-bios-setting foo bar --no-reboot-check
    expect_rc 3
fi

if [ -x /usr/bin/certtool ]; then

    # ---
    echo "Building unsigned ${CAB}..."
    INPUT="@installedtestsdir@/fakedevice124.bin \
              @installedtestsdir@/fakedevice124.metainfo.xml"
    run build-cabinet ${CAB} ${INPUT} --force
    expect_rc 0

    # ---
    echo "Sign ${CAB}"
    @installedtestsdir@/build-certs.py /tmp
    run firmware-sign ${CAB} /tmp/testuser.pem /tmp/testuser.key --json
    expect_rc 0

    # ---
    echo "Cleaning self-signed ${CAB}..."
    rm -f ${CAB}
fi

if [ -z "$CI_NETWORK" ]; then
    echo "Skipping remaining tests due to CI_NETWORK not being set"
    exit 0
fi

# ---
echo "Refresh remotes"
run refresh --json
expect_rc 0

# check we can search for known tokens
run search --json CVE-2022-21894
expect_rc 0
run search --json KEK
expect_rc 0
run search --json org.uefi.dbx
expect_rc 0
run search --json linux
expect_rc 0

# check we do not find a random search result
run search --json DOESNOTEXIST
expect_rc 3
