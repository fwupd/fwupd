# Setting up Android

> TLDR: enable root, disable selinux, `/data` is exec and rw

Since we're dealing with USB devices we want wireless debugging (Wired Ethernet would be nice but it doesn't seem possible)

```bash
adb connect {ip address}:{port}
```

You'll need root

```bash
# Set adb to root mode
adb -e root
```

This command may timeout as the adb server on the device will restart with a new port. You will probably need to reconnect.

Figure out where you can write to and run executables from in my case this is `/data`

```bash
# List mountpoints that are exec and rw
adb -e shell mount | grep -v noexec | grep rw
```

If there's no suitable mountpoint you can use `adb -e shell mount -oremount,rw`.

You will need selinux to be permissive for clients to interact with the binder interface.

```bash
# You can log SELinux issues with
adb -e logcat 'SELinux:*' '*:S'
```

```bash
# Get SELinux to ignore issues
adb -e shell setenforce permissive
```

# Building and running on Android

## 1. Point the `ndk_path` field to your `ANDROID_NDK_HOME` in `contrib/ci/android_arm64-cross-file.ini`

```ini
[constants]
ndk_path = '/opt/android/android-ndk-r27/'
```

## 2. Configure

Setting the prefix to a directory that is writeable on the device is important as fwupd will exit if the prefix cache path is not writeable ignoring the value of the `CACHE_DIRECTORY` environment variable.

```bash
meson setup --cross-file contrib/ci/android_arm64-cross-file.ini --prefix=/data/fwupd _android_build
```

This also disables libjcat features to sidestep the gpgme and gnutls dependencies.

## 3. Build

```bash
meson install --destdir=$(pwd)/_android_dist -C _android_build
```

## 4. Upload to Device

```bash
./adb-push-sync.sh _android_dist/data/fwupd/ /data/fwupd
```

This script is basically just `tar -cOC ${1} . | adb shell tar x -C ${2} -f -` to avoid issues I've had with `adb push` not updating the build.

## 5. Run

```bash
./adb_fwupd_env.sh fwupd-binder --verbose
```

This script originally set environment variables to identify paths but since we're using the correct prefix that is unnecessary.  
Currently the script just sets the correct `LD_LIBRARY_PATH` and `PATH` environment.