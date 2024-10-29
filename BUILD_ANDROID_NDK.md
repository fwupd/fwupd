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

# Debugging

## logcat 

[logcat](https://developer.android.com/tools/logcat) can be used for general Android logging:

```bash
adb -e logcat 'fwupd:*' 'AndroidRuntime:*' 'TransactionExecutor:*' 'SELinux:*' '*:S'
```

## Kernel binder tracing

Write `1` to `/sys/kernel/tracing/tracing_on` and `/sys/kernel/tracing/events/binder/enable`.

Read from `/sys/kernel/tracing/trace` or `/sys/kernel/tracing/trace_pipe`.

For example:
```bash
adb -e shell -t '\
 echo 1 > /sys/kernel/tracing/events/binder/enable ; \
 echo 1 > /sys/kernel/tracing/tracing_on ; \
 cat /sys/kernel/tracing/trace_pipe \
'
```

`/sys/kernel/tracing/events/binder/` also contains subdirectories which can be used to filter specific binder functions.

Example output of `/sys/kernel/tracing/trace_pipe`
```
 eedesktop.fwupd-3153  [001] .... 462762.700092: binder_ioctl: cmd=0xc0306201 arg=0x7fd42a0190
 eedesktop.fwupd-3153  [001] .... 462762.700104: binder_command: cmd=0x40406300 BC_TRANSACTION
 eedesktop.fwupd-3153  [001] .... 462762.700295: binder_transaction: transaction=3889277 dest_node=3879782 dest_proc=1196 dest_thread=0 reply=0 flags=0x11 code=0x3
 eedesktop.fwupd-3153  [001] .... 462762.700301: binder_update_page_range: proc=1196 allocate=1 offset=0 size=4096
 eedesktop.fwupd-3153  [001] .... 462762.700304: binder_alloc_lru_start: proc=1196 page_index=0
 eedesktop.fwupd-3153  [001] .... 462762.700308: binder_alloc_lru_end: proc=1196 page_index=0
 eedesktop.fwupd-3153  [001] .... 462762.700314: binder_transaction_alloc_buf: transaction=3889277 data_size=88 offsets_size=0
 eedesktop.fwupd-3153  [001] ...2 462762.700328: binder_set_priority: proc=1196 thread=1227 old=120 => new=98 desired=98
 eedesktop.fwupd-3153  [001] .... 462762.700401: binder_write_done: ret=0
 eedesktop.fwupd-3153  [001] .... 462762.700406: binder_wait_for_work: proc_work=0 transaction_stack=0 thread_todo=1
 eedesktop.fwupd-3153  [001] .... 462762.700414: binder_return: cmd=0x7206 BR_TRANSACTION_COMPLETE
 eedesktop.fwupd-3153  [001] .... 462762.700418: binder_read_done: ret=0
 eedesktop.fwupd-3153  [001] .... 462762.700421: binder_ioctl_done: ret=0

```