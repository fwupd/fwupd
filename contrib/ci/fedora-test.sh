#!/bin/sh
set -ex

group() {
    { echo "${CI:+::group::}🔵 $*"; } 2>/dev/null
}

endgroup() {
    { [ -n "${CI:-}" ] && echo "::endgroup::" || true; } 2>/dev/null
}

# workaround dnf bug
export FORCE_COLUMNS=100

group "Update system and install dependencies"
dnf install -y dist/*.rpm
dnf install -y gcovr
endgroup

group "Enable test devices"
fwupdtool enable-test-devices
endgroup

group "Set up PolicyKit and D-Bus to run the daemon"
mkdir -p /run/dbus
mkdir -p /var
ln -s /var/run /run
dbus-daemon --system --fork
/usr/lib/polkit-1/polkitd &
sleep 5
endgroup

# Enable testing capturing emulation data
fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018

# run the daemon startup to check it can start
/usr/libexec/fwupd/fwupd --immediate-exit --verbose

# run the installed tests whilst the daemon debugging
NO_COLOR=1 G_DEBUG=fatal-criticals /usr/libexec/fwupd/fwupd --verbose --no-timestamp >fwupd.txt 2>&1 &
sleep 10

group "Run the tests via gnome-desktop-testing-runner"
gnome-desktop-testing-runner --timeout=2400 fwupd
endgroup

# generate coverage report
./contrib/ci/coverage.sh
