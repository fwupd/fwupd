#!/bin/sh -e

exec 2>&1

# only run as root, possibly only in CI
if [ "$(id -u)" -ne 0 ]; then exit 0; fi

error() {
    if [ -f "fwupd.txt" ]; then
        cat fwupd.txt
    else
        journalctl -u fwupd -b || true
    fi
    echo "exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

# ---
echo "Starting P2P daemon..."
export FWUPD_DBUS_SOCKET="/run/fwupd.sock"
rm -rf ${FWUPD_DBUS_SOCKET}
@libexecdir@/fwupd/fwupd -v --timed-exit --no-timestamp &
while [ ! -e ${FWUPD_DBUS_SOCKET} ]; do sleep 1; done

# ---
echo "Starting P2P client..."
fwupdmgr get-devices --json
expect_rc 0

# ---
echo "Shutting down P2P daemon..."
fwupdmgr quit

# success!
exit 0
