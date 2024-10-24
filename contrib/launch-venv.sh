#!/usr/bin/env -S bash -e
gcc=$(gcc -dumpmachine)
DIST="$(dirname $0)/../dist"
BIN="$(basename $0)"
POLICY="/usr/share/dbus-1/system.d/org.freedesktop.fwupd.conf"
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
ENV="FWUPD_POLKIT_NOCHECK=1 \
     G_DEBUG=${G_DEBUG} \
     LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
for var in $(env | grep FWUPD | cut -d= -f1); do
        ENV="${ENV} ${var}=${!var}"
done
SUDO=$(which sudo)
if [ "${BIN}" = "fwupd-binder" ] || \
   [ "${BIN}" = "fwupd" ] && \
   [ -d "$(dirname ${POLICY})" ] && \
   [ ! -f ${POLICY} ]; then
        echo "Missing D-Bus policy in ${POLICY}"
        echo "Copy into filesystem? [y/N]"
        read -r answer
        if [ "${answer}" != "y" ]; then
                exit 1
        fi
        ${SUDO} cp ${DIST}/share/dbus-1/system.d/org.freedesktop.fwupd.conf ${POLICY}
fi
${SUDO} ${ENV} ${DEBUG} ${EXE} "$@"
