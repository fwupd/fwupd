#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Install the Android NDK, SDK build-tools, system image and the Bionic runtime
# needed to cross-build fwupd and run the result on the host.

import os
import shlex
import shutil
import stat
import subprocess
import tempfile
import urllib.request
import zipfile

# Android NDK
NDK_VERSION_NAME = "r27d"
NDK_ZIP_FILE = f"android-ndk-{NDK_VERSION_NAME}-linux.zip"
NDK_DOWNLOAD_URL = f"https://dl.google.com/android/repository/{NDK_ZIP_FILE}"
NDK_INSTALL_PATH = "/opt/android"
ANDROID_NDK_HOME = f"{NDK_INSTALL_PATH}/android-ndk-{NDK_VERSION_NAME}"
NDK_PREBUILT = f"{ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64"
NDK_SYSROOT_LIB = f"{NDK_PREBUILT}/sysroot/usr/lib/x86_64-linux-android"

# Android SDK
ANDROID_SDK_ROOT = "/opt/android/sdk"
CMDLINE_TOOLS_URL = "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
SDKMANAGER = f"{ANDROID_SDK_ROOT}/cmdline-tools/bin/sdkmanager"
BUILD_TOOLS_VERSION = "35.0.0"
SYSIMG = f"{ANDROID_SDK_ROOT}/system-images/android-35/default/x86_64"


def run(*args: str, **kwargs) -> None:
    print(f"+ {shlex.join(args)}", flush=True)
    subprocess.run(args, check=True, **kwargs)


def unzip(filename: str, dest: str) -> None:
    # ZipFile.extractall() drops the permissions and writes symlinks as regular
    # files, and the NDK toolchain needs both
    with zipfile.ZipFile(filename) as zf:
        for info in zf.infolist():
            path = zf.extract(info, dest)
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                target = zf.read(info).decode()
                os.unlink(path)
                os.symlink(target, path)
            elif mode and not info.is_dir():
                os.chmod(path, stat.S_IMODE(mode))


def download_and_unzip(url: str, dest: str) -> None:
    os.makedirs(dest, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".zip") as f:
        print(f"downloading {url}", flush=True)
        with urllib.request.urlopen(url) as response:
            shutil.copyfileobj(response, f)
        f.flush()
        print(f"extracting to {dest}", flush=True)
        unzip(f.name, dest)


def sdkmanager_install(package: str) -> None:
    # accept all the license prompts
    run(
        SDKMANAGER,
        f"--sdk_root={ANDROID_SDK_ROOT}",
        package,
        input="y\n" * 100,
        text=True,
    )


def install_ndk() -> None:
    if os.path.exists(f"{NDK_PREBUILT}/bin/clang"):
        print(f"Android NDK already installed at {ANDROID_NDK_HOME}")
        return
    download_and_unzip(NDK_DOWNLOAD_URL, NDK_INSTALL_PATH)


def install_build_tools() -> None:
    if os.path.isfile(f"{ANDROID_SDK_ROOT}/build-tools/{BUILD_TOOLS_VERSION}/aidl"):
        print("Android SDK Build-Tools already downloaded")
        return
    download_and_unzip(CMDLINE_TOOLS_URL, ANDROID_SDK_ROOT)
    sdkmanager_install(f"build-tools;{BUILD_TOOLS_VERSION}")


def install_rust() -> None:
    run("rustup", "default", "stable")
    run("rustup", "target", "add", "x86_64-linux-android")


def install_system_image() -> None:
    if os.path.isfile(f"{SYSIMG}/system.img"):
        print("Android system image already downloaded")
        return
    sdkmanager_install("system-images;android-35;default;x86_64")


def install_lpunpack() -> None:
    if shutil.which("lpunpack"):
        return
    run("pip", "install", "--break-system-packages", "liblp")


def install_bionic_runtime() -> None:
    if os.path.exists("/system/bin/linker64"):
        print("Android Bionic runtime already installed")
        return

    # system.img is GPT containing a super.img with logical partitions;
    # linker64, libc, libm and libdl are inside the com.android.runtime APEX
    with tempfile.TemporaryDirectory(prefix="android-extract-") as tmpdir:

        def extract(archive: str, *members: str) -> None:
            run("7z", "e", f"-o{tmpdir}", archive, *members, "-y")

        extract(f"{SYSIMG}/system.img", "1.super.img")
        run("lpunpack", "-o", tmpdir, f"{tmpdir}/1.super.img")
        extract(
            f"{tmpdir}/system.img",
            "system/apex/com.android.runtime.apex",
            "system/lib64/liblog.so",
            "system/lib64/libz.so",
        )
        extract(f"{tmpdir}/com.android.runtime.apex", "apex_payload.img")
        extract(
            f"{tmpdir}/apex_payload.img",
            "bin/linker64",
            "lib64/bionic/libc.so",
            "lib64/bionic/libdl.so",
            "lib64/bionic/libm.so",
        )
        os.makedirs("/system/bin", exist_ok=True)
        os.makedirs("/system/lib64", exist_ok=True)
        shutil.move(f"{tmpdir}/linker64", "/system/bin/linker64")
        os.chmod("/system/bin/linker64", 0o755)
        for lib in ["libc.so", "libm.so", "libdl.so", "liblog.so", "libz.so"]:
            shutil.move(f"{tmpdir}/{lib}", f"/system/lib64/{lib}")


def install_ndk_runtime() -> None:
    """C++ and binder runtime from the NDK"""
    if os.path.exists("/system/lib64/libc++_shared.so"):
        print("Android NDK runtime libraries already installed")
        return
    os.makedirs("/system/lib64", exist_ok=True)
    for lib in ["libc++_shared.so", "35/libbinder_ndk.so"]:
        shutil.copy(
            f"{NDK_SYSROOT_LIB}/{lib}", f"/system/lib64/{os.path.basename(lib)}"
        )


def main() -> None:
    install_ndk()
    install_build_tools()
    install_rust()
    install_system_image()
    install_lpunpack()
    install_bionic_runtime()
    install_ndk_runtime()


if __name__ == "__main__":
    main()
