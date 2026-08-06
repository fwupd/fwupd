#!/bin/sh
set -e
set -x

# Android NDK
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

# Android Build-Tools
if [ -f "${ANDROID_SDK_ROOT}/build-tools/35.0.0/aidl" ]; then
    echo "Android SDK Build-Tools already downloaded"
else
    wget -q "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip" -O /tmp/cmdline-tools.zip
    mkdir -p "${ANDROID_SDK_ROOT}"
    unzip -q /tmp/cmdline-tools.zip -d "${ANDROID_SDK_ROOT}"
    rm "/tmp/cmdline-tools.zip"
    yes | "${ANDROID_SDK_ROOT}/cmdline-tools/bin/sdkmanager" \
        --sdk_root="${ANDROID_SDK_ROOT}" "build-tools;35.0.0"
fi

rustup default stable
rustup target add x86_64-linux-android
