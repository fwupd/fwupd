#!/bin/sh

exec 2>&1

export NO_COLOR=1
export FWUPD_VERBOSE=1
CAB=fakedevice124.cab
INPUT="@installedtestsdir@/fakedevice124.bin \
       @installedtestsdir@/fakedevice124.jcat \
       @installedtestsdir@/fakedevice124.metainfo.xml"
DEVICE=08d460be0f1f9f128413f816022a6439e0078018
FWUPDTOOL="fwupdtool -v"

# ---
echo "Show help output"
${FWUPDTOOL} --help
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Show version output"
${FWUPDTOOL} --version
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Showing hwids"
${FWUPDTOOL} hwids
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Showing plugins"
${FWUPDTOOL} get-plugins
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Showing plugins (json)"
${FWUPDTOOL} get-plugins --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Enabling test device..."
${FWUPDTOOL} enable-test-devices
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Checking device-flags"
${FWUPDTOOL} get-device-flags
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Checking firmware-gtypes"
${FWUPDTOOL} get-firmware-gtypes
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Checking firmware-types"
${FWUPDTOOL} get-firmware-types
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Checking for updates"
${FWUPDTOOL} get-updates
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Checking for updates"
${FWUPDTOOL} get-updates --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Building ${CAB}..."
${FWUPDTOOL} build-cabinet ${CAB} ${INPUT} --force
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Examining ${CAB}..."
${FWUPDTOOL} get-details ${CAB}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Installing ${CAB} cabinet..."
${FWUPDTOOL} install ${CAB}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Cleaning ${CAB} generated cabinet ..."
rm -f ${CAB}

# ---
echo "Verifying update..."
${FWUPDTOOL} verify-update ${DEVICE}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting history (should be one)..."
${FWUPDTOOL} get-history
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Clearing history..."
${FWUPDTOOL} clear-history ${DEVICE}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting history (should be none)..."
${FWUPDTOOL} get-history
rc=$?; if [ $rc != 2 ]; then exit $rc; fi

# ---
echo "Resetting config..."
${FWUPDTOOL} reset-config test
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Testing good version compare"
${FWUPDTOOL} vercmp 1.0.0 1.0.0 triplet
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Testing bad version compare"
${FWUPDTOOL} vercmp 1.0.0 1.0.1 foo
rc=$?; if [ $rc != 1 ]; then exit $rc; fi

# ---
echo "Getting supported version formats..."
${FWUPDTOOL} get-version-formats --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting report metadata..."
${FWUPDTOOL} get-report-metadata --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting the list of remotes"
${FWUPDTOOL} get-remotes --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Disabling LVFS remote..."
${FWUPDTOOL} modify-remote lvfs Enabled false
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Enabling LVFS remote..."
${FWUPDTOOL} modify-remote lvfs Enabled true
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Modify unknown remote (should fail)..."
${FWUPDTOOL} modify-remote foo Enabled true
rc=$?; if [ $rc != 1 ]; then exit $rc; fi

# ---
echo "Modify known remote but unknown key (should fail)..."
${FWUPDTOOL} modify-remote lvfs bar true
rc=$?; if [ $rc != 3 ]; then exit $rc; fi

# ---
echo "Getting devices (should be one)..."
${FWUPDTOOL} get-devices --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Changing VALID config on test device..."
${FWUPDTOOL} modify-config test AnotherWriteRequired true
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Changing INVALID config on test device...(should fail)"
${FWUPDTOOL} modify-config test Foo true
rc=$?; if [ $rc != 1 ]; then exit $rc; fi

# ---
echo "Disabling test device..."
${FWUPDTOOL} disable-test-devices
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

BASEDIR=@installedtestsdir@/tests/bios-attrs/dell-xps13-9310/
if [ -d $BASEDIR ]; then
       WORKDIR="$(mktemp -d)"
       cp $BASEDIR $WORKDIR -r
       export FWUPD_SYSFSFWATTRIBDIR=$WORKDIR/dell-xps13-9310/
       # ---
       echo "Get BIOS settings..."
       ${FWUPDTOOL} get-bios-settings --json
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Get BIOS setting as json..."
       ${FWUPDTOOL} get-bios-settings WlanAutoSense --json
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Get BIOS setting as a string..."
       ${FWUPDTOOL} get-bios-settings WlanAutoSense
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Modify BIOS setting to different value..."
       ${FWUPDTOOL} set-bios-setting WlanAutoSense Enabled  --no-reboot-check
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Modify BIOS setting back to default..."
       ${FWUPDTOOL} set-bios-setting WlanAutoSense Disabled  --no-reboot-check
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Modify BIOS setting to bad value (should fail)..."
       ${FWUPDTOOL} set-bios-setting WlanAutoSense foo  --no-reboot-check
       rc=$?; if [ $rc != 1 ]; then exit $rc; fi

       # ---
       echo "Modify Unknown BIOS setting (should fail)..."
       ${FWUPDTOOL} set-bios-setting foo bar  --no-reboot-check
       rc=$?; if [ $rc != 3 ]; then exit $rc; fi
fi

if [ -z "$CI_NETWORK" ]; then
        echo "Skipping remaining tests due to CI_NETWORK not being set"
        exit 0
fi

# ---
echo "Refresh remotes"
${FWUPDTOOL} refresh --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi
