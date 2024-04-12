---
title: Environment Variables
---

When running fwupd reads some variables from your environment and changes some
behavior. This might be useful for debugging, or to make fwupd run somewhere
with a non-standard filesystem layout.

## fwupdmgr and fwupdtool

* `DISABLE_SSL_STRICT` disables strict SSL certificate checking, which may make
  downloading files work when using some antisocial corporate firewalls.
* `FWUPD_CURL_VERBOSE` shows more information when downloading files
* `FWUPD_SUPPORTED` overrides the `-Dsupported_build` meson option at runtime
* `FWUPD_VERBOSE` is set when running `--verbose`
* `FWUPD_XMLB_VERBOSE` can be set to show Xmlb silo regeneration and quirk matches
* `FWUPD_DBUS_SOCKET` is used to set the socket filename if running without a dbus-daemon
* `FWUPD_PROFILE` can be used to set the profile traceback threshold value in ms
* `FWUPD_FUZZER_RUNNING` if the firmware format is being fuzzed
* `FWUPD_POLKIT_NOCHECK` if we should not check for polkit policies to be installed
* standard glibc variables like `LANG` are also honored for CLI tools that are translated
* libcurl respects the session proxy, e.g. `http_proxy`, `all_proxy`, `sftp_proxy` and `no_proxy`

## daemon

* `FWUPD_MACHINE_KIND` can be used to override the detected machine type, e.g. `physical`, `virtual`, or `container`
* `FWUPD_HOST_EMULATE` can be used to load test data from `/usr/share/fwupd/host-emulate.d`, e.g. `thinkpad-p1-no-iommu.json.gz`
* `FWUPD_SYSCALL_FILTER` can be set to the name of the service manager if syscalls are being filtered, e.g. `systemd`.

## Self Tests

* `CI_NETWORK` if CI is running with network access
* `TPM_SERVER_RUNNING` if an emulated TPM is running
* `UMOCKDEV_DIR` if set, running under umockdev

Other variables, include:

* `FWUPD_DELL_FAKE_SMBIOS` if set, use fake SMBIOS information for tests
* `FWUPD_FORCE_TPM2` ignores a TPM 1.2 device detected in the TPM self tests
* `FWUPD_REDFISH_SELF_TEST` if set, do destructive tests on the actual device BMC
* `FWUPD_REDFISH_SMBIOS_DATA` use this filename to emulate a specific SMBIOS blob
* `FWUPD_SOLOKEY_EMULATE` emulates a fake device for testing
* `FWUPD_UEFI_CAPSULE_RECREATE_COD_DATA` if set, write the files in the example COD tree in srcdir
* `FWUPD_UEFI_TEST` used by the UEFI plugins to disable specific sanity checks during self tests
* `FWUPD_MACHINE_ID` used by the tests to set a predictable hash normally loaded from `/etc/machine-id`

## File system overrides

These are not fully documented here, see <https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-common.c>
for details.

* `CACHE_DIRECTORY`
* `CONFIGURATION_DIRECTORY`
* `FWUPD_ACPITABLESDIR`
* `FWUPD_DATADIR`
* `FWUPD_DATADIR_QUIRKS`
* `FWUPD_EFIAPPDIR`
* `FWUPD_FIRMWARESEARCH`
* `FWUPD_HOSTDIR` looks for host OS `os-release` in this sysroot, default is /
* `FWUPD_LIBDIR_PKG`
* `FWUPD_LOCALSTATEDIR`
* `FWUPD_LOCALSTATEDIR_QUIRKS`
* `FWUPD_OFFLINE_TRIGGER`
* `FWUPD_PROCFS`
* `FWUPD_SYSCONFDIR`
* `FWUPD_SYSFSDMIDIR`
* `FWUPD_SYSFSDRIVERDIR`
* `FWUPD_SYSFSFWATTRIBDIR`
* `FWUPD_SYSFSFWDIR`
* `FWUPD_SYSFSSECURITYDIR`
* `FWUPD_SYSFSTPMDIR`
* `FWUPD_LOCKDIR`
* `FWUPD_UEFI_ESP_PATH`
* `HOME`
* `RUNTIME_DIRECTORY`
* `SNAP`
* `SNAP_USER_DATA`
* `STATE_DIRECTORY`
