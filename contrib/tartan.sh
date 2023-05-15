#!/bin/sh
/usr/bin/scan-build -load-plugin /usr/local/lib64/tartan/16.0/libtartan.so \
	-disable-checker unix.Malloc \
	-disable-checker core.CallAndMessage \
	-disable-checker deadcode.DeadStores \
	-enable-checker tartan.GErrorChecker \
	-v --view "$@"
