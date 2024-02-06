#!/bin/sh -e
#install RPM packages
dnf install -y dist/*.rpm

fwupdtool enable-test-devices

# set up enough PolicyKit and D-Bus to run the daemon
mkdir -p /run/dbus
mkdir -p /var
ln -s /var/run /run
dbus-daemon --system --fork
/usr/lib/polkit-1/polkitd &
sleep 5

# run the daemon startup to check it can start
/usr/libexec/fwupd/fwupd --immediate-exit --verbose

# run the installed tests whilst the daemon debugging
/usr/libexec/fwupd/fwupd --verbose &
sleep 10
gnome-desktop-testing-runner fwupd
