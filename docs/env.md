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
* `FWUPD_DEVICE_TESTS_BASE_URI` sets the base URI when downloading firmware for the device-tests
* `FWUPD_SUPPORTED` overrides the `-Dsupported_build` meson option at runtime
* `FWUPD_VERBOSE` is set when running `--verbose`
* `FWUPD_BACKEND_VERBOSE` can be used to show detailed plugin and backend debugging
* `FWUPD_XMLB_VERBOSE` can be set to show Xmlb silo regeneration and quirk matches
* `FWUPD_DBUS_SOCKET` is used to set the socket filename if running without a dbus-daemon
* `FWUPD_DOWNLOAD_VERBOSE` can be used to show wget or curl output
* `FWUPD_PROFILE` can be used to set the profile traceback threshold value in ms
* `FWUPD_FUZZER_RUNNING` if the firmware format is being fuzzed
* standard glibc variables like `LANG` are also honored for CLI tools that are translated
* libcurl respects the session proxy, e.g. `http_proxy`, `all_proxy`, `sftp_proxy` and `no_proxy`

## daemon

* `FWUPD_MACHINE_KIND` can be used to override the detected machine type, e.g. `physical`, `virtual`, or `container`
* `FWUPD_HOST_EMULATE` can be used to load test data from `/usr/share/fwupd/host-emulate.d`, e.g. `thinkpad-p1-no-iommu.json.gz`

## Self Tests

* `CI_NETWORK` if CI is running with network access
* `TPM_SERVER_RUNNING` if an emulated TPM is running

## Shared libfwupdplugin

* `FU_HID_DEVICE_VERBOSE` shows HID traffic
* `FU_SREC_FIRMWARE_VERBOSE` shows more information about parsing
* `FU_UDEV_DEVICE_DEBUG` shows more information about UDEV devices, including parents
* `FU_USB_DEVICE_DEBUG` shows more information about USB devices
* `FWUPD_DEVICE_LIST_VERBOSE` display devices being added and removed from the list
* `FWUPD_PROBE_VERBOSE` dump the detected devices to the console, even if not supported by fwupd
* `FWUPD_BIOS_SETTING_VERBOSE` be verbose while parsing BIOS settings

## Plugins

Most plugins read a plugin-specific runtime key to increase verbosity more than the usual `VERBOSE`.
This can be also used when using fwupdtool e.g. using `--plugin-verbose=dell` will set the
environment variable of `FWUPD_DELL_VERBOSE` automatically.

Other variables, include:

* `FWUPD_DELL_FAKE_SMBIOS` if set, use fake SMBIOS information for tests
* `FWUPD_FORCE_TPM2` ignores a TPM 1.2 device detected in the TPM self tests
* `FWUPD_PLUGIN_TEST` used by the test plugin to pass data out-of-band to the loader
* `FWUPD_REDFISH_SELF_TEST` if set, do destructive tests on the actual device BMC
* `FWUPD_REDFISH_SMBIOS_DATA` use this filename to emulate a specific SMBIOS blob
* `FWUPD_SOLOKEY_EMULATE` emulates a fake device for testing
* `FWUPD_SUPERIO_DISABLE_MIRROR` disables the e-flash fixup to get byte-accurate hardware dumps
* `FWUPD_SUPERIO_RECOVER` allow recovery of a corrupted SuperIO by hardcoding the device size
* `FWUPD_TEST_PLUGIN_XML` used by the test plugin to load XML state out-of-band before startup
* `FWUPD_UEFI_CAPSULE_RECREATE_COD_DATA` if set, write the files in the example COD tree in srcdir
* `FWUPD_UEFI_TEST` used by the UEFI plugins to disable specific sanity checks during self tests
* `FWUPD_WAC_EMULATE` emulates a fake device for testing

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
* `FWUPD_LOCALSTATEDIR`
* `FWUPD_LOCALSTATEDIR_QUIRKS`
* `FWUPD_OFFLINE_TRIGGER`
* `FWUPD_PLUGINDIR`
* `FWUPD_PROCFS`
* `FWUPD_SYSCONFDIR`
* `FWUPD_SYSFSDRIVERDIR`
* `FWUPD_SYSFSFWATTRIBDIR`
* `FWUPD_SYSFSFWDIR`
* `FWUPD_SYSFSSECURITYDIR`
* `FWUPD_SYSFSTPMDIR`
* `FWUPD_UEFI_ESP_PATH`
* `HOME`
* `RUNTIME_DIRECTORY`
* `SNAP`
* `SNAP_USER_DATA`
* `STATE_DIRECTORY`
