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

error()
{
       rc=$1
       cat fwupdtool.txt
       exit $rc
}

run()
{
       cmd="fwupdtool -v $*"
       echo "cmd: $cmd" >fwupdtool.txt
       $cmd 1>>fwupdtool.txt 2>&1
}

# ---
echo "Show help output"
run --help
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Show version output"
run --version
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Show version output (json)"
run --version --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Showing hwids"
run hwids
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Showing plugins"
run get-plugins
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Showing plugins (json)"
run get-plugins --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Enabling test device..."
run enable-test-devices
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Checking device-flags"
run get-device-flags
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Checking firmware-gtypes"
run get-firmware-gtypes
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Checking firmware-types"
run get-firmware-types
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Checking for updates"
run get-updates
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Checking for updates"
run get-updates --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Building ${CAB}..."
run build-cabinet ${CAB} ${INPUT} --force
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Examining ${CAB}..."
run get-details ${CAB}
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Installing ${CAB} cabinet..."
run install ${CAB}
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Cleaning ${CAB} generated cabinet ..."
rm -f ${CAB}

# ---
echo "Verifying update..."
run verify-update ${DEVICE}
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Getting history (should be one)..."
run get-history
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Clearing history..."
run clear-history ${DEVICE}
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Getting history (should be none)..."
run get-history
rc=$?; if [ $rc != 2 ]; then error $rc; fi

# ---
echo "Resetting config..."
run reset-config test
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Testing good version compare"
run vercmp 1.0.0 1.0.0 triplet
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Testing bad version compare"
run vercmp 1.0.0 1.0.1 foo
rc=$?; if [ $rc != 1 ]; then error $rc; fi

# ---
echo "Getting supported version formats..."
run get-version-formats --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Getting report metadata..."
run get-report-metadata --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Getting the list of remotes"
run get-remotes --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Disabling LVFS remote..."
run modify-remote lvfs Enabled false
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Enabling LVFS remote..."
run modify-remote lvfs Enabled true
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Modify unknown remote (should fail)..."
run modify-remote foo Enabled true
rc=$?; if [ $rc != 1 ]; then error $rc; fi

# ---
echo "Modify known remote but unknown key (should fail)..."
run modify-remote lvfs bar true
rc=$?; if [ $rc != 3 ]; then error $rc; fi

# ---
echo "Getting devices (should be one)..."
run get-devices --json
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Changing VALID config on test device..."
run modify-config test AnotherWriteRequired true
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Changing INVALID config on test device...(should fail)"
run modify-config test Foo true
rc=$?; if [ $rc != 1 ]; then error $rc; fi

# ---
echo "Disabling test device..."
run disable-test-devices
rc=$?; if [ $rc != 0 ]; then error $rc; fi

BASEDIR=@installedtestsdir@/tests/bios-attrs/dell-xps13-9310/
if [ -d $BASEDIR ]; then
       WORKDIR="$(mktemp -d)"
       cp $BASEDIR $WORKDIR -r
       export FWUPD_SYSFSFWATTRIBDIR=$WORKDIR/dell-xps13-9310/
       # ---
       echo "Get BIOS settings..."
       run get-bios-settings --json
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

       # ---
       echo "Get BIOS setting as json..."
       run get-bios-settings WlanAutoSense --json
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

       # ---
       echo "Get BIOS setting as a string..."
       run get-bios-settings WlanAutoSense
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

       # ---
       echo "Modify BIOS setting to different value..."
       run set-bios-setting WlanAutoSense Enabled  --no-reboot-check
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

       # ---
       echo "Modify BIOS setting back to default..."
       run set-bios-setting WlanAutoSense Disabled  --no-reboot-check
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

       # ---
       echo "Modify BIOS setting to bad value (should fail)..."
       run set-bios-setting WlanAutoSense foo  --no-reboot-check
       rc=$?; if [ $rc != 1 ]; then error $rc; fi

       # ---
       echo "Modify Unknown BIOS setting (should fail)..."
       run set-bios-setting foo bar  --no-reboot-check
       rc=$?; if [ $rc != 3 ]; then error $rc; fi
fi

if [ -x /usr/bin/certtool ]; then

       # ---
       echo "Building unsigned ${CAB}..."
       INPUT="@installedtestsdir@/fakedevice124.bin \
              @installedtestsdir@/fakedevice124.metainfo.xml"
       run build-cabinet ${CAB} ${INPUT} --force
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

       # ---
       echo "Sign ${CAB}"
       @installedtestsdir@/build-certs.py /tmp
       run firmware-sign ${CAB} /tmp/testuser.pem /tmp/testuser.key --json
       rc=$?; if [ $rc != 0 ]; then error $rc; fi

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
rc=$?; if [ $rc != 0 ]; then error $rc; fi
