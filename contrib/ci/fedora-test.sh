#!/bin/sh -e
#install RPM packages
dnf install -y dist/*.rpm
dnf install -y gcovr

fwupdtool enable-test-devices

# set up enough PolicyKit and D-Bus to run the daemon
mkdir -p /run/dbus
mkdir -p /var
ln -s /var/run /run
dbus-daemon --system --fork
/usr/lib/polkit-1/polkitd &
sleep 5

# Enable testing capturing emulation data
fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018

# run the daemon startup to check it can start
/usr/libexec/fwupd/fwupd --immediate-exit --verbose

# run the installed tests whilst the daemon debugging
NO_COLOR=1 G_DEBUG=fatal-criticals /usr/libexec/fwupd/fwupd --verbose --no-timestamp >fwupd.txt 2>&1 &
sleep 10
gnome-desktop-testing-runner --timeout=600 fwupd

# generate coverage report
./contrib/ci/coverage.sh
