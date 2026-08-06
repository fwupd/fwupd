#!/bin/sh
set -e
set -x

# check for and install missing dependencies
if [ -e "/usr/bin/apt-get" ]; then
    ./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes --os ubuntu --variant android
fi

# check for NDK, SDK and Bionic runtime
./contrib/ci/android.sh

# make aidl available
export ANDROID_SDK_ROOT="/opt/android/sdk"
export PATH="${PATH}:${ANDROID_SDK_ROOT}/cmdline-tools/bin:${ANDROID_SDK_ROOT}/build-tools/35.0.0"

root=$(pwd)
export BUILD="${root}/build"
rm -rf "${BUILD}"
meson "${BUILD}" \
    -Dman=false \
    -Ddocs=enabled \
    -Dlibxmlb:gtkdoc=false \
    -Db_sanitize=address \
    --cross-file contrib/android/android_x86_64-cross-file.ini \
    --prefix="${root}/target"

ninja -C "${BUILD}" -v
meson test -C "${BUILD}" --print-errorlogs --verbose
