#!/bin/sh
set -e

exec 2>&1

# only run as root, possibly only in CI
if [ "$(id -u)" -ne 0 ]; then exit 0; fi

# ---
echo "Starting P2P daemon..."
export FWUPD_DBUS_SOCKET="/var/run/fwupd.sock"
rm -rf ${FWUPD_DBUS_SOCKET}
@libexecdir@/fwupd/fwupd --verbose --timed-exit --no-timestamp &
while [ ! -e ${FWUPD_DBUS_SOCKET} ]; do sleep 1; done

# ---
echo "Starting P2P client..."
fwupdmgr get-devices --json
rc=$?; if [ $rc != 0 ]; then exit $rc; fi

# ---
echo "Shutting down P2P daemon..."
gdbus call --system --dest org.freedesktop.fwupd --object-path / --method org.freedesktop.fwupd.Quit

# success!
exit 0
