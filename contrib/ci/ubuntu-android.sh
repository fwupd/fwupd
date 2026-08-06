#!/bin/sh
set -e
set -x

# check for and install missing dependencies
if [ -e "/usr/bin/apt-get" ]; then
    ./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes --os ubuntu --variant android
fi

# check for NDK, SDK and Bionic runtime
./contrib/ci/android.sh

# make aidl and exe_wrapper available
export ANDROID_SDK_ROOT="/opt/android/sdk"
export PATH="${PWD}/contrib/android:${PATH}:${ANDROID_SDK_ROOT}/cmdline-tools/bin:${ANDROID_SDK_ROOT}/build-tools/35.0.0"

root=$(pwd)
export BUILD="${root}/build"
rm -rf "${BUILD}"
meson setup "${BUILD}" \
    -Db_coverage=true \
    --cross-file contrib/android/android_x86_64-cross-file.ini \
    --prefix="${root}/target"

ninja -C "${BUILD}" -v
ninja -C "${BUILD}" test

# generate coverage report using llvm-cov from the NDK
export GCOV="/opt/android/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-cov gcov"
./contrib/ci/coverage.sh
