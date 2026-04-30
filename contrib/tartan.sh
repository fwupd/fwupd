#!/bin/sh
set -eu

plugin=/usr/lib64/tartan/18.1/libtartan.so
[ -f "$plugin" ] || plugin="$(systemd-path system-library-arch)/tartan/18.1/libtartan.so"
[ -f "$plugin" ] || {
    echo "failed to find tartan plugin" 1>&2
    exit 1
}

/usr/bin/scan-build-18 \
    -load-plugin "$plugin" \
    -disable-checker core.CallAndMessage \
    -disable-checker core.NullDereference \
    -disable-checker deadcode.DeadStores \
    -disable-checker unix.Malloc \
    -enable-checker tartan.GErrorChecker \
    --status-bugs -v "$@"
