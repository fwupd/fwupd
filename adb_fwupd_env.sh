#!/usr/bin/env bash

# meson setup --cross-file ../fwupd/contrib/ci/android_arm64-cross-file.ini --reconfigure --prefix=$(pwd)/dist _android_build
# meson install -C _android_build
# adb -e shell rm -r /system_ext/bin/fwupd
# adb -e push dist/ /system_ext/bin/fwupd
# ./adb_fwupd_env.sh fwupdtool get-devices -vv
# ./adb_fwupd_env.sh fwupd-binder --verbose

FWUPD_DIST_PATH="/system_ext/bin/fwupd"

adb -e shell -t \
 FWUPD_POLKIT_NOCHECK=1 \
 FWUPD_SYSCONFDIR="${FWUPD_DIST_PATH}/etc" \
 CACHE_DIRECTORY="${FWUPD_DIST_PATH}/var/cache/fwupd" \
 FWUPD_DATADIR="${FWUPD_DIST_PATH}/usr/share/fwupd" \
 FWUPD_LIBEXECDIR="${FWUPD_DIST_PATH}/libexec" \
 LD_LIBRARY_PATH="${FWUPD_DIST_PATH}/lib/fwupd-2.0.2:${FWUPD_DIST_PATH}/lib64:${FWUPD_DIST_PATH}/lib" \
 PATH="\${PATH}:${FWUPD_DIST_PATH}/bin:${FWUPD_DIST_PATH}/libexec/fwupd" \
 "${*}"
