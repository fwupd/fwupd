#!/bin/sh
set -ex

group() {
    { echo "${CI:+::group::}🔵 $*"; } 2>/dev/null
}

endgroup() {
    { [ -n "${CI:-}" ] && echo "::endgroup::" || true; } 2>/dev/null
}

group "Install RPM packages"
dnf install -y dist/*.rpm
endgroup

group "Set up PolicyKit and D-Bus to run the daemon"
mkdir -p /run/dbus
mkdir -p /var
ln -s /var/run /run
dbus-daemon --system --fork
/usr/lib/polkit-1/polkitd &
sleep 5
endgroup

# run the daemon startup to check it can start
/usr/libexec/fwupd/fwupd --immediate-exit --verbose
