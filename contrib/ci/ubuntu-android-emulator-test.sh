#!/usr/bin/env bash
#
# Install fwupd onto a running Android emulator and verify the fwupd daemon and
# fwupdmgr talk to each other over binder.
#
# Unlike fwupdtool (see contrib/ci/ubuntu-android-build.sh) the daemon and
# fwupdmgr link the AIDL interface stubs, whose global constructors register the
# binder interface class with libbinder_ndk at load time. That only works on a
# real Android binder stack, so this test needs a booted device/emulator rather
# than the host-side Bionic wrapper.
#
# Expects an emulator already booted and visible to `adb` (the GitHub job uses
# reactivecircus/android-emulator-runner). Run locally against a device with:
#   DESTDIR="$PWD/dist-android" meson install -C build
#   ./contrib/ci/ubuntu-android-emulator-test.sh dist-android
set -euo pipefail

# the install tree staged by `meson install --destdir` (default: dist-android)
DIST="${1:-dist-android}"

# must match --prefix in contrib/ci/ubuntu-android-build.sh; the binaries' rpath
# and the baked-in paths assume the tree lives here on-device
PREFIX="/data/fwupd"
# hard-coded as localstatedir for android in the top-level meson.build
LOCALSTATEDIR="/data/vendor/fwupd"
SERVICE="org.freedesktop.fwupd.IFwupd/default"

DAEMON="${PREFIX}/libexec/fwupd/fwupd"
FWUPDMGR="${PREFIX}/bin/fwupdmgr"
DAEMON_LOG="/data/local/tmp/fwupd-daemon.log"
# redirect coverage (.gcda) writes to a writable path so instrumented binaries
# do not fail trying to write to the host build directory baked in at compile
# time -- harmless if the tree was built without -Db_coverage
GCOV_ENV="GCOV_PREFIX=/data/local/tmp/gcov GCOV_PREFIX_STRIP=99"

# VINTF manifest fragment declaring the VINTF-stable binder service, and where it
# must live on-device for servicemanager to read it at boot
HERE="$(cd "$(dirname "$0")" && pwd)"
VINTF_FRAGMENT="${HERE}/../android/vintf/org.freedesktop.fwupd.xml"
VINTF_DEST="/vendor/etc/vintf/manifest/org.freedesktop.fwupd.xml"

if [ ! -d "${DIST}${PREFIX}" ]; then
    echo "error: ${DIST}${PREFIX} not found -- run 'meson install --destdir' first" >&2
    exit 1
fi

# wait until the device is both connected and finished booting
wait_boot() {
    adb wait-for-device
    for _ in $(seq 1 60); do
        [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && return 0
        sleep 5
    done
    echo "error: device did not finish booting" >&2
    return 1
}

adb wait-for-device
# AOSP userdebug images allow rooting; needed to write /data and relax SELinux
adb root
wait_boot

# Declare the VINTF-stable fwupd binder service. servicemanager refuses to
# register or resolve a VINTF-stable instance that is not in the device VINTF
# manifest ("Could not find org.freedesktop.fwupd.IFwupd/default in the VINTF
# manifest"), so install the fragment onto the (read-only) vendor image and
# reboot for servicemanager to pick it up. Skip the work if it is already there.
if ! adb shell "test -f ${VINTF_DEST}" >/dev/null 2>&1; then
    echo "installing VINTF fragment and rebooting..."
    adb remount >/dev/null 2>&1 || true
    adb disable-verity >/dev/null 2>&1 || true
    adb reboot
    wait_boot
    adb root
    wait_boot
    adb remount
    adb shell "mkdir -p $(dirname "${VINTF_DEST}")"
    adb push "${VINTF_FRAGMENT}" "${VINTF_DEST}"
    adb reboot
    wait_boot
    adb root
    wait_boot
fi

# SELinux also has to allow a root caller to add/find the service
adb shell setenforce 0 || true

cleanup() {
    echo "=== fwupd daemon log ==="
    adb shell "cat ${DAEMON_LOG} 2>/dev/null" || true
    echo "=== logcat (fwupd) ==="
    adb logcat -d 2>/dev/null | grep -i fwupd || true
    adb shell "pkill -f ${DAEMON}" 2>/dev/null || true
}
trap cleanup EXIT

# push the install tree to where the binaries expect to find themselves; use the
# tar-based sync so file modes and symlinks are preserved on-device (plain
# `adb push` drops the executable bit)
adb shell "rm -rf ${PREFIX}"
ADB_FLAGS="-e" "${HERE}/../android/adb-push-sync.sh" "${DIST}${PREFIX}" "${PREFIX}"
adb shell "mkdir -p ${LOCALSTATEDIR}/run ${PREFIX}/cache /data/local/tmp/gcov"

# shared library search path: the versioned private libdir plus the main libdir
LIBDIR_PKG="$(adb shell "echo ${PREFIX}/lib/fwupd-*" | tr -d '\r')"
ENV_COMMON="FWUPD_POLKIT_NOCHECK=1 \
CACHE_DIRECTORY=${PREFIX}/cache \
FWUPD_LOCKDIR=${LOCALSTATEDIR}/run \
LD_LIBRARY_PATH=${LIBDIR_PKG}:${PREFIX}/lib \
${GCOV_ENV}"

# start the daemon detached; it keeps running after this adb shell returns
echo "starting fwupd daemon..."
adb shell "G_MESSAGES_DEBUG=all ${ENV_COMMON} nohup ${DAEMON} >${DAEMON_LOG} 2>&1 </dev/null &"

# wait for the daemon to register its binder service
echo "waiting for ${SERVICE} to register..."
registered=false
for _ in $(seq 1 30); do
    if adb shell service list 2>/dev/null | grep -q "org.freedesktop.fwupd.IFwupd"; then
        registered=true
        break
    fi
    sleep 2
done
if [ "${registered}" != true ]; then
    echo "error: fwupd daemon did not register ${SERVICE}" >&2
    exit 1
fi
echo "daemon registered"

# exercise the client -> daemon binder path; any non-zero exit fails the job
echo "=== fwupdmgr get-plugins ==="
adb shell "${ENV_COMMON} ${FWUPDMGR} get-plugins"
echo "=== fwupdmgr get-devices ==="
adb shell "${ENV_COMMON} ${FWUPDMGR} get-devices"

echo "android emulator smoke test passed"
