#!/bin/sh
set -e
set -x

# check for and install missing dependencies
./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes --os ubuntu --variant android

# setup Android NDK
export NDK_VERSION_NAME="r27d"
export NDK_ZIP_FILE="android-ndk-${NDK_VERSION_NAME}-linux.zip"
export NDK_DOWNLOAD_URL="https://dl.google.com/android/repository/${NDK_ZIP_FILE}"
export NDK_INSTALL_PATH="/opt/android"
export ANDROID_NDK_HOME="${NDK_INSTALL_PATH}/android-ndk-${NDK_VERSION_NAME}"
export ANDROID_SDK_ROOT="/opt/android/sdk"

if [ -e "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" ]; then
    echo "Android NDK already installed at ${ANDROID_NDK_HOME}"
else
    mkdir -p "${NDK_INSTALL_PATH}"
    wget -q "${NDK_DOWNLOAD_URL}" -O "/tmp/${NDK_ZIP_FILE}"
    unzip -q "/tmp/${NDK_ZIP_FILE}" -d "${NDK_INSTALL_PATH}"

    rm "/tmp/${NDK_ZIP_FILE}"
fi

if [ -f "${ANDROID_SDK_ROOT}/build-tools/35.0.0/aidl" ]; then
    echo "Android SDK Build-Tools already installed"
else
    wget -q "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" -O /tmp/cmdline-tools.zip
    mkdir -p "${ANDROID_SDK_ROOT}"
    unzip -q /tmp/cmdline-tools.zip -d "${ANDROID_SDK_ROOT}"
    rm "/tmp/cmdline-tools.zip"
    # accept all license prompts
    yes | "${ANDROID_SDK_ROOT}/cmdline-tools/bin/sdkmanager" \
        --sdk_root="${ANDROID_SDK_ROOT}" "build-tools;35.0.0"
fi

export PATH="${PATH}:${ANDROID_SDK_ROOT}/cmdline-tools/bin:${ANDROID_SDK_ROOT}/build-tools/35.0.0"

# install Rust target for Android cross-compilation
if ! command -v rustup >/dev/null 2>&1; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
    . "${HOME}/.cargo/env"
fi
rustup target add x86_64-linux-android

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
