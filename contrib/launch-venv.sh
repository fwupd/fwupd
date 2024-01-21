#!/bin/sh -e
gcc=$(gcc -dumpmachine)
DIST="$(dirname $0)/../dist"
BIN="$(basename $0)"
export FWUPD_LOCALSTATEDIR=${DIST}
export FWUPD_SYSCONFDIR=${DIST}/etc
export LD_LIBRARY_PATH=${DIST}/lib/${gcc}:${DIST}/lib64:${DIST}/lib
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
if [ ! -f ${EXE} ]; then
        echo "Not yet built! Please run:"
        echo ""
        echo "# build-fwupd"
        exit 1
fi
SUDO="$(which sudo) \
        LD_LIBRARY_PATH=${LD_LIBRARY_PATH} \
        FWUPD_LOCALSTATEDIR=${FWUPD_LOCALSTATEDIR} \
        FWUPD_SYSCONFDIR=${FWUPD_SYSCONFDIR} \
        G_DEBUG=fatal_warnings \
        FWUPD_POLKIT_NOCHECK=1"
${SUDO} ${DEBUG} ${EXE} "$@"
