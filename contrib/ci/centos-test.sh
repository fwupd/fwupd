#!/bin/sh -e

#install RPM packages
dnf install -y dist/*.rpm

# set up enough PolicyKit and D-Bus to run the daemon
mkdir -p /run/dbus
mkdir -p /var
ln -s /var/run /run
dbus-daemon --system --fork
/usr/lib/polkit-1/polkitd &
sleep 5

# run the daemon startup to check it can start
/usr/libexec/fwupd/fwupd --immediate-exit --verbose
