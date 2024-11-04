#!/bin/sh

exec 2>&1

export NO_COLOR=1
export FWUPD_VERBOSE=1
CAB=fakedevice124.cab
INPUT="@installedtestsdir@/fakedevice124.bin \
       @installedtestsdir@/fakedevice124.jcat \
       @installedtestsdir@/fakedevice124.metainfo.xml"
DEVICE=08d460be0f1f9f128413f816022a6439e0078018

# ---
echo "Show help output"
fwupdtool --help
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Show version output"
fwupdtool --version
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Enabling test device..."
fwupdtool enable-test-devices
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Building ${CAB}..."
fwupdtool build-cabinet ${CAB} ${INPUT} --force
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Examining ${CAB}..."
fwupdtool get-details ${CAB}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Installing ${CAB} cabinet..."
fwupdtool install ${CAB}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Cleaning ${CAB} generated cabinet ..."
rm -f ${CAB}

# ---
echo "Verifying update..."
fwupdtool verify-update ${DEVICE}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting history (should be one)..."
fwupdtool get-history
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Clearing history..."
fwupdtool clear-history ${DEVICE}
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting history (should be none)..."
fwupdtool get-history
rc=$?; if [ $rc != 2 ]; then exit $rc; fi

# ---
echo "Resetting config..."
fwupdtool reset-config test
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Testing good version compare"
fwupdtool vercmp 1.0.0 1.0.0 triplet
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Testing bad version compare"
fwupdtool vercmp 1.0.0 1.0.1 foo
rc=$?; if [ $rc != 1 ]; then exit $rc; fi

# ---
echo "Getting supported version formats..."
fwupdtool get-version-formats --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Getting the list of remotes"
fwupdtool get-remotes --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Disabling LVFS remote..."
fwupdtool modify-remote lvfs Enabled false
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Enabling LVFS remote..."
fwupdtool modify-remote lvfs Enabled true
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Modify unknown remote (should fail)..."
fwupdtool modify-remote foo Enabled true
rc=$?; if [ $rc != 1 ]; then error $rc; fi

# ---
echo "Getting devices (should be one)..."
fwupdtool get-devices --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Changing VALID config on test device..."
fwupdtool modify-config test AnotherWriteRequired true
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Changing INVALID config on test device...(should fail)"
fwupdtool modify-config test Foo true
rc=$?; if [ $rc != 1 ]; then error $rc; fi

# ---
echo "Disabling test device..."
fwupdtool disable-test-devices
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

BASEDIR=@installedtestsdir@/tests/bios-attrs/dell-xps13-9310/
if [ -d $BASEDIR ]; then
       WORKDIR="$(mktemp -d)"
       cp $BASEDIR $WORKDIR -r
       export FWUPD_SYSFSFWATTRIBDIR=$WORKDIR/dell-xps13-9310/
       # ---
       echo "Get BIOS settings..."
       fwupdtool get-bios-settings --json
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Get BIOS setting..."
       fwupdtool get-bios-settings WlanAutoSense --json
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Modify BIOS setting to different value..."
       fwupdtool set-bios-setting WlanAutoSense Enabled  --no-reboot-check
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Modify BIOS setting back to default..."
       fwupdtool set-bios-setting WlanAutoSense Disabled  --no-reboot-check
       rc=$?; if [ $rc != 0 ]; then exit $rc; fi

       # ---
       echo "Modify BIOS setting to bad value (should fail)..."
       fwupdtool set-bios-setting WlanAutoSense foo  --no-reboot-check
       rc=$?; if [ $rc != 1 ]; then exit $rc; fi

       # ---
       echo "Modify Unknown BIOS setting (should fail)..."
       fwupdtool set-bios-setting foo bar  --no-reboot-check
       rc=$?; if [ $rc != 3 ]; then exit $rc; fi
fi

if [ -z "$CI_NETWORK" ]; then
        echo "Skipping remaining tests due to CI_NETWORK not being set"
        exit 0
fi

# ---
echo "Refresh remotes"
fwupdtool refresh --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi
