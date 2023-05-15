#!/bin/sh
/usr/bin/scan-build -load-plugin /usr/local/lib64/tartan/16.0/libtartan.so \
	-disable-checker core.CallAndMessage \
	-disable-checker core.NullDereference \
	-disable-checker deadcode.DeadStores \
	-disable-checker unix.Malloc \
	-enable-checker tartan.GErrorChecker \
	--status-bugs -v "$@"
