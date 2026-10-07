#!/bin/sh
set -e
set -x

# check for NDK, SDK and Bionic runtime
./contrib/ci/android.sh

# make aidl and exe_wrapper available
export ANDROID_SDK_ROOT="/opt/android/sdk"
export PATH="${PWD}/contrib/android:${PATH}:${ANDROID_SDK_ROOT}/cmdline-tools/bin:${ANDROID_SDK_ROOT}/build-tools/35.0.0"

root=$(pwd)
export BUILD="${root}/build"
rm -rf "${BUILD}"
# install to /data/fwupd so the baked-in rpath and paths match where the tree is
# pushed on-device by the emulator job (contrib/ci/ubuntu-android-emulator-test.sh)
meson setup "${BUILD}" \
    -Db_coverage=true \
    --cross-file contrib/android/android_x86_64-cross-file.ini \
    --cross-file contrib/ci/android-ci.ini \
    --prefix=/data/fwupd

ninja -C "${BUILD}" -v
ninja -C "${BUILD}" test

# smoke-test the cross-built fwupdtool runs under the host-side Bionic wrapper
export FWUPD_LOCKDIR="${BUILD}/lockdir"
mkdir -p "${FWUPD_LOCKDIR}"
android-exe-wrapper.sh "${BUILD}/src/fwupdtool" --version
android-exe-wrapper.sh "${BUILD}/src/fwupdtool" get-plugins

# stage an install tree for the emulator job to push onto a real Android device,
# packaged as a tarball so file modes and symlinks survive the CI artifact round
# trip (actions/upload-artifact zips and would otherwise drop the exec bit)
rm -rf "${root}/dist-android"
DESTDIR="${root}/dist-android" meson install -C "${BUILD}"
tar -C "${root}/dist-android" -czf "${root}/dist-android.tar.gz" .

# generate coverage report using llvm-cov from the NDK
export GCOV="/opt/android/android-ndk-r27d/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-cov gcov"
./contrib/ci/coverage.sh
