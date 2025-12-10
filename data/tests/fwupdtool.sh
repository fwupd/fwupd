#!/bin/sh

exec 0>/dev/null
exec 2>&1

TMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR"' EXIT

export NO_COLOR=1
export FWUPD_VERBOSE=1
CAB=fakedevice124.cab
INPUT="@installedtestsdir@/fakedevice124.bin \
       @installedtestsdir@/fakedevice124.jcat \
       @installedtestsdir@/fakedevice124.metainfo.xml"
DEVICE=08d460be0f1f9f128413f816022a6439e0078018

error() {
    cat fwupdtool.txt
    echo " ● exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

run() {
    cmd="fwupdtool -v --plugins test $*"
    echo " ● cmd: $cmd" >fwupdtool.txt
    $cmd 1>>fwupdtool.txt 2>&1
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
echo " ● Show version output (JSON)"
run --version --json
expect_rc 0

# ---
echo " ● Showing hwids"
run hwids
expect_rc 0

# ---
echo " ● Exporting hwids"
run export-hwids ${TMPDIR}/hwids.ini
expect_rc 0

UNAME=$(uname -m)
if [ "${UNAME}" = "x86_64" ] || [ "${UNAME}" = "i686" ]; then
    EXPECTED=0
else
    EXPECTED=1
fi
# ---
echo " ● Showing security"
run security
expect_rc $EXPECTED

# ---
echo " ● Showing plugins"
run get-plugins
expect_rc 0

# ---
echo " ● Showing plugins (JSON)"
run get-plugins --json
expect_rc 0

# ---
echo " ● Checking device-flags"
run get-device-flags
expect_rc 0

# ---
echo " ● Checking firmware-gtypes"
run get-firmware-gtypes
expect_rc 0

# ---
echo " ● Checking firmware-types"
run get-firmware-types
expect_rc 0

# ---
echo " ● Building ${CAB}…"
run build-cabinet ${CAB} ${INPUT} --force
expect_rc 0

# ---
echo " ● Examining ${CAB}…"
run get-details ${CAB}
expect_rc 0

# ---
echo " ● Testing good version compare"
run vercmp 1.0.0 1.0.0 triplet
expect_rc 0

# ---
echo " ● Testing bad version compare"
run vercmp 1.0.0 1.0.1 foo
expect_rc 1

# ---
echo " ● Getting supported version formats…"
run get-version-formats
expect_rc 0

# ---
echo " ● Getting supported version formats (JSON)…"
run get-version-formats --json
expect_rc 0

# ---
echo " ● Getting report metadata…"
run get-report-metadata
expect_rc 0

# ---
echo " ● Getting report metadata (JSON)…"
run get-report-metadata --json
expect_rc 0

# ---
echo " ● Getting the list of remotes"
run get-remotes --json
expect_rc 0

# ---
echo " ● Disabling LVFS remote…"
run modify-remote lvfs Enabled false
expect_rc 0

# ---
echo " ● Enabling LVFS remote…"
run modify-remote lvfs Enabled true
expect_rc 0

# ---
echo " ● Modify unknown remote (should fail)…"
run modify-remote foo Enabled true
expect_rc 1

# ---
echo " ● Modify known remote but unknown key (should fail)…"
run modify-remote lvfs bar true
expect_rc 3

BASEDIR=@installedtestsdir@/tests/bios-attrs/dell-xps13-9310/
if [ -d $BASEDIR ]; then
    WORKDIR="$(mktemp -d)"
    cp $BASEDIR $WORKDIR -r
    export FWUPD_SYSFSFWATTRIBDIR=$WORKDIR/dell-xps13-9310/
    # ---
    echo " ● Get BIOS settings…"
    run get-bios-settings --json
    expect_rc 0

    # ---
    echo " ● Get BIOS setting as json…"
    run get-bios-settings WlanAutoSense --json
    expect_rc 0

    # ---
    echo " ● Get BIOS setting as a string…"
    run get-bios-settings WlanAutoSense
    expect_rc 0

    # ---
    echo " ● Modify BIOS setting to different value…"
    run set-bios-setting WlanAutoSense Enabled --no-reboot-check
    expect_rc 0

    # ---
    echo " ● Modify BIOS setting back to default…"
    run set-bios-setting WlanAutoSense Disabled --no-reboot-check
    expect_rc 0

    # ---
    echo " ● Modify BIOS setting to bad value (should fail)…"
    run set-bios-setting WlanAutoSense foo --no-reboot-check
    expect_rc 1

    # ---
    echo " ● Modify Unknown BIOS setting (should fail)…"
    run set-bios-setting foo bar --no-reboot-check
    expect_rc 3
fi

if [ -x /usr/bin/certtool ]; then
    # ---
    echo " ● Sign ${CAB}"
    @installedtestsdir@/build-certs.py ${TMPDIR}
    run firmware-sign ${CAB} ${TMPDIR}/testuser.pem ${TMPDIR}/testuser.key --json
    expect_rc 0
fi

# ---
echo " ● Firmware builder…"
echo "<firmware gtype=\"FuSrecFirmware\"><id>HDR</id><data>aGVsbG8gd29ybGQ=</data></firmware>" >${TMPDIR}/blob.builder.xml
run firmware-build ${TMPDIR}/blob.builder.xml ${TMPDIR}/blob.srec
expect_rc 0

# ---
echo " ● Firmware parse blob…"
run firmware-parse ${TMPDIR}/blob.srec srec
expect_rc 0

# ---
echo " ● Firmware parse blob (auto)…"
run firmware-parse ${TMPDIR}/blob.srec auto
expect_rc 0

# ---
echo " ● Firmware extract…"
run firmware-extract ${TMPDIR}/blob.srec srec
expect_rc 0

# ---
echo " ● Firmware export…"
run firmware-export ${TMPDIR}/blob.srec srec
expect_rc 0

# ---
echo " ● Firmware convert (not working)…"
run firmware-convert ${TMPDIR}/blob.srec ${TMPDIR}/blob.bin srec srec
expect_rc 3

if [ -z "$CI_NETWORK" ]; then
    echo " ● Skipping remaining tests due to CI_NETWORK not being set"
    exit 0
fi

# ---
echo " ● Clean remote"
run clean-remote lvfs
expect_rc 0

# ---
echo " ● Refresh remotes (forced)"
run refresh --json --force --verbose
expect_rc 0

# ---
echo " ● Showing remote ages…"
run get-remotes --json --verbose
expect_rc 0

# ---
echo " ● Refreshing (already up to date)…"
run refresh @localstatedir@/lib/fwupd/metadata/lvfs/firmware.xml.zst @localstatedir@/lib/fwupd/metadata/lvfs/firmware.xml.zst.jcat lvfs
expect_rc 2

# ---
echo " ● Search for known tokens…"
run search --json CVE-2022-21894
expect_rc 0
run search --json KEK
expect_rc 0
run search --json org.uefi.dbx
expect_rc 0
run search --json linux
expect_rc 0

# ---
echo " ● Search for random search result…"
run search --json DOESNOTEXIST
expect_rc 3

# ---
echo " ● Verify test device is present"
fwupdtool get-devices --json | jq -e '.Devices | any(.Plugin == "test")'
if [ $? != 0 ]; then
    echo " ● Skipping tests due to no test device enabled"
    exit 0
fi

# ---
echo " ● Clearing history…"
run clear-history ${DEVICE}
expect_rc 0

# ---
echo " ● Installing blob…"
echo " ● 0.0.0" >${TMPDIR}/blob.bin
run install-blob ${TMPDIR}/blob.bin ${DEVICE}
expect_rc 0

# ---
echo " ● Dumping blob (not possible)…"
run firmware-dump ${TMPDIR}/blob.bin ${DEVICE} --force
expect_rc 1

# ---
echo " ● Reading blob (not possible)…"
run firmware-read ${TMPDIR}/blob.bin ${DEVICE} --force
expect_rc 1

# ---
echo " ● Installing ${CAB} cabinet…"
run install ${CAB}
expect_rc 0

# ---
echo " ● Installing same version (not possible)…"
run reinstall ${DEVICE}
expect_rc 1

# ---
echo " ● Cleaning ${CAB} generated cabinet…"
rm -f ${CAB}

# ---
echo " ● Verifying update…"
run verify-update ${DEVICE}
expect_rc 0

# ---
echo " ● Getting history (should be one)…"
run get-history
expect_rc 0

# ---
echo " ● Clearing history…"
run clear-history ${DEVICE}
expect_rc 0

# ---
echo " ● Getting history (should be none)…"
run get-history
expect_rc 2

# ---
echo " ● Activating (not required)…"
run activate ${DEVICE}
expect_rc 2

# ---
echo " ● Activating (not possible)…"
run switch-branch ${DEVICE} impossible
expect_rc 2

# ---
echo " ● Resetting config…"
run reset-config test
expect_rc 0

# ---
echo " ● Getting devices (should be one)…"
run get-devices --json
expect_rc 0

# ---
echo " ● Getting one device (should be one)…"
run get-devices ${DEVICE}
expect_rc 0

# ---
echo " ● Switching branch…"
run switch-branch ${DEVICE}
expect_rc 1

# ---
echo " ● Getting all devices, even unsupported ones…"
run get-devices --show-all --force
expect_rc 0

# ---
echo " ● Changing VALID config on test device…"
run modify-config test AnotherWriteRequired true
expect_rc 0

# ---
echo " ● Changing INVALID config on test device…(should fail)"
run modify-config test Foo true
expect_rc 1

# ---
echo " ● Checking for updates"
run get-updates
expect_rc 0

# ---
echo " ● Checking for updates"
run get-updates --json
expect_rc 0

# ---
echo " ● Resetting config…"
run reset-config test
expect_rc 0

# ---
echo " ● Calculating CRCs…"
echo "hello world" >${TMPDIR}/crc.txt
run crc b8-autosar ${TMPDIR}/crc.txt
expect_rc 0
run crc b16-xmodem ${TMPDIR}/crc.txt
expect_rc 0
run crc b32-standard ${TMPDIR}/crc.txt
expect_rc 0

# ---
echo " ● Calculating CRCs (invalid)…"
run crc ${TMPDIR}/crc.txt
expect_rc 1
run crc invalid ${TMPDIR}/crc.txt
expect_rc 1

# ---
echo " ● Finding CRC…"
run crc-find ${TMPDIR}/crc.txt
expect_rc 1
run crc-find 0xaf083b2d ${TMPDIR}/crc.txt
expect_rc 0
run crc-find 0x12345678 ${TMPDIR}/crc.txt
expect_rc 1
