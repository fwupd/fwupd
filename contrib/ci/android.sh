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

# Android system image
export SYSIMG="${ANDROID_SDK_ROOT}/system-images/android-35/default/x86_64"
if [ -f "${SYSIMG}/system.img" ]; then
    echo "Android system image already downloaded"
else
    yes | "${ANDROID_SDK_ROOT}/cmdline-tools/bin/sdkmanager" \
        --sdk_root="${ANDROID_SDK_ROOT}" "system-images;android-35;default;x86_64"
fi

# lpunpack
if ! command -v lpunpack >/dev/null 2>&1; then
    pip install --break-system-packages liblp
fi

# Bionic runtime
if [ -e "/system/bin/linker64" ]; then
    echo "Android Bionic runtime already installed"
else
    # system.img is GPT containing a super.img with logical partitions;
    # linker64, libc, libm and libdl are inside the com.android.runtime APEX
    7z e -o/tmp/android-extract "${SYSIMG}/system.img" '1.super.img' -y
    lpunpack -o /tmp/android-extract /tmp/android-extract/1.super.img
    7z e -o/tmp/android-extract /tmp/android-extract/system.img \
        'system/apex/com.android.runtime.apex' 'system/lib64/liblog.so' \
        'system/lib64/libz.so' -y
    7z e -o/tmp/android-extract /tmp/android-extract/com.android.runtime.apex \
        'apex_payload.img' -y
    7z e -o/tmp/android-extract /tmp/android-extract/apex_payload.img \
        'bin/linker64' 'lib64/bionic/libc.so' 'lib64/bionic/libdl.so' \
        'lib64/bionic/libm.so' -y
    mkdir -p /system/bin /system/lib64
    mv /tmp/android-extract/linker64 /system/bin/linker64
    chmod +x /system/bin/linker64
    for lib in libc.so libm.so libdl.so liblog.so libz.so; do
        mv "/tmp/android-extract/$lib" "/system/lib64/$lib"
    done
    rm -rf /tmp/android-extract
    # C++ and binder runtime from the NDK
    NDK_SYSROOT_LIB="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/x86_64-linux-android"
    for lib in libc++_shared.so 35/libbinder_ndk.so; do
        cp "${NDK_SYSROOT_LIB}/$lib" "/system/lib64/$(basename "$lib")"
    done
fi
