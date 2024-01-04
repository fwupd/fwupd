#!/bin/sh
gcc=$(gcc -dumpmachine)
DIST="$(dirname $0)"
BIN="$(basename $0)"
export FWUPD_LOCALSTATEDIR=${DIST}
export FWUPD_SYSCONFDIR=${DIST}/etc
export LD_LIBRARY_PATH=${DIST}/lib/${gcc}:${DIST}/lib64
if [ -n "${DEBUG}" ]; then
        if ! which gdbserver 1>/dev/null 2>&1; then
                echo "install gdbserver to enable debugging"
                exit 1
        fi
        DEBUG="gdbserver localhost:9091"
fi
if [ -f ${DIST}/libexec/fwupd/${BIN} ]; then
        EXE=${DIST}/libexec/fwupd/${BIN}
else
        EXE=${DIST}/bin/${BIN}
fi
${DEBUG} ${EXE} "$@"
