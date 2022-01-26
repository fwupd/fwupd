#!/bin/sh
gcc=$(gcc -dumpmachine)
export ROOT=#ROOT#
export FWUPD_LOCALSTATEDIR=${ROOT}/dist
export FWUPD_SYSCONFDIR=${ROOT}/dist/etc
export LD_LIBRARY_PATH=${ROOT}/dist/lib/${gcc}
if [ -n "${DEBUG}" ]; then
        DEBUG="gdbserver localhost:9091"
fi
${DEBUG} ${ROOT}/dist/#EXECUTABLE# "$@"
