#!/bin/bash

SDK_PATH="$HOME/Android/Sdk"
BUILD_TOOLS_VER="35.0.0"
AIDL_BIN="$SDK_PATH/build-tools/$BUILD_TOOLS_VER/aidl"

SOURCE_BASE="contrib/android/aidl"
OUT_DIR="./aidl_generated"

echo "Cleaning old generated files..."
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/include"

echo "Generating NDK-backend C++ files..."

$AIDL_BIN --lang=ndk \
    --stability=vintf \
    --structured \
    -I "$SOURCE_BASE" \
    -I "$SDK_PATH/ndk/$(ls $SDK_PATH/ndk | head -1)/sysroot/usr/include" \
    --out "$OUT_DIR" \
    --header_out "$OUT_DIR/include" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdInstallOptions.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdDevice.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdHwid.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdInstallRequest.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdRequest.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdProperties.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdMetadata.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdRemote.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/FwupdUpdate.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/IFwupd.aidl" \
    "$SOURCE_BASE/org/freedesktop/fwupd/IFwupdEventListener.aidl"

if [ $? -eq 0 ]; then
    echo "SUCCESS: Files generated in $OUT_DIR"
else
    echo "FAILED."
    exit 1
fi
