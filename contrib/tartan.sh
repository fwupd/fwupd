#!/bin/sh
/usr/bin/scan-build \
	-load-plugin /usr/lib64/tartan/19.1/libtartan.so \
	-disable-checker core.CallAndMessage \
	-disable-checker core.NullDereference \
	-disable-checker deadcode.DeadStores \
	-disable-checker unix.Malloc \
	-enable-checker tartan.GErrorChecker \
	--status-bugs -v "$@"
