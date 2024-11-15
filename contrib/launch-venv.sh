#!/usr/bin/env -S bash -e
gcc=$(gcc -dumpmachine)
DIST="$(dirname $0)/../dist"
BIN="$(basename $0)"
COMMAND="$1"
ARGUMENT="$2"
DBUSPOLICY="/usr/share/dbus-1/system.d/org.freedesktop.fwupd.conf"
PKPOLICY="/usr/share/polkit-1/actions/org.freedesktop.fwupd.policy"
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
if [ -z "${G_DEBUG}" ]; then
        G_DEBUG="fatal-criticals"
fi
ENV="FWUPD_POLKIT_NOCHECK=1 \
     G_DEBUG=${G_DEBUG} \
     LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"
for var in $(env | grep FWUPD | cut -d= -f1); do
        ENV="${ENV} ${var}=${!var}"
done
SUDO=$(which sudo)
if [ "${BIN}" = "fwupd" ] && \
   [ -d "$(dirname ${DBUSPOLICY})" ] && \
   [ ! -f ${DBUSPOLICY} ]; then
        echo "Missing D-Bus policy in ${DBUSPOLICY}"
        echo "Copy into filesystem? [y/N]"
        read -r answer
        if [ "${answer}" != "y" ]; then
                exit 1
        fi
        ${SUDO} cp ${DIST}/share/dbus-1/system.d/org.freedesktop.fwupd.conf ${DBUSPOLICY}
fi
if [ "${BIN}" = "fwupdmgr" ] &&
   [ -d "$(dirname ${PKPOLICY})" ] && \
   ! grep "org.freedesktop.fwupd.emulation-save" $PKPOLICY 1>/dev/null 2>&1; then
        echo "Missing or outdated PolicyKit policy in ${PKPOLICY}"
        echo "Copy into filesystem? [y/N]"
        read -r answer
        if [ "${answer}" != "y" ]; then
                exit 1
        fi
        ${SUDO} cp ${DIST}/share/polkit-1/actions/org.freedesktop.fwupd.policy ${PKPOLICY}
fi
${SUDO} ${ENV} ${DEBUG} ${EXE} "$@"

if [ "${BIN}" = "fwupdmgr" ] && [ "${COMMAND}" = "emulation-save" ]; then
        ${SUDO} chown "$(id -u)":"$(id -g)" ${ARGUMENT}
fi
