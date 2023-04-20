---
title: Configuration File Format
---

% fwupd.conf(5) {{PACKAGE_VERSION}} | Configuration File Format

NAME
----

**fwupd.conf** — configuration file for the fwupd daemon.

SYNOPSIS
--------

The `{{SYSCONFDIR}}/fwupd/fwupd.conf` file is the main configuration file for the fwupd daemon.
The complete description of the file format and possible parameters are documented here for reference purposes.

FILE FORMAT
-----------

The file consists of a multiple sections with optional parameters. Parameters are of the form:

  [section]
  key = value

The file is line-based, each newline-terminated line represents either a comment, a section name or
a parameter.

Section and parameter names are case sensitive.

Only the first equals sign in a parameter is significant.
Whitespace before or after the first equals sign is discarded as is leading and trailing whitespace
in a parameter value.
Internal whitespace within a parameter value is retained.

Any line beginning with a hash (`#`) character is ignored, as are lines containing only whitespace.

The values following the equals sign in parameters are all either a string (no quotes needed),
unsigned integers, or a boolean, which may be given as **true** or **false**.
Case is not significant in boolean values, but is preserved in string values.

DAEMON PARAMETERS
-----------------

The `[daemon]` section can contain the following parameters.

**DisabledDevices=**

  Allow blocking specific devices by their GUID, using semicolons as delimiter.

**DisabledPlugins={{FU_DAEMON_CONFIG_DEFAULT_DISABLED_PLUGINS}}**

  Allow blocking specific plugins by name.
  Use **fwupdmgr get-plugins** to get the list of plugins.

**ArchiveSizeMax=**

  Maximum archive size that can be loaded in Mb, with 25% of the total system memory as the default.

**IdleTimeout=**

  Idle time in seconds to shut down the daemon, where a value of **0** specifies "never".

  **NOTE:** some plugins might inhibit the auto-shutdown, for instance thunderbolt.

**VerboseDomains=**

  Comma separated list of domains to log in verbose mode.
  If unset, no domains are set to verbose.
  If set to "*", all domains are verbose, which is the same as running the daemon with **--verbose --verbose**.

**UpdateMotd={{FU_DAEMON_CONFIG_DEFAULT_UPDATE_MOTD}}**

  Update the message of the day (MOTD) on device and metadata changes.

**EnumerateAllDevices={{FU_DAEMON_CONFIG_DEFAULT_ENUMERATE_ALL_DEVICES}}**

  For some plugins, enumerate only devices supported by metadata.

**ApprovedFirmware=**

  A list of firmware checksums that has been approved by the site admin
  If unset, all firmware is approved.

**BlockedFirmware=**

  Allow blocking specific devices by their cabinet checksum, either SHA-1 or SHA-256.

**UriSchemes={{FU_DAEMON_CONFIG_DEFAULT_URI_SCHEMES}}**

  Allowed URI schemes in the preference order; failed downloads from the first scheme will be retried with the next in order until no choices remain.

**IgnorePower={{FU_DAEMON_CONFIG_DEFAULT_IGNORE_POWER}}**

  Ignore power levels of devices when running updates.

**OnlyTrusted={{FU_DAEMON_CONFIG_DEFAULT_ONLY_TRUSTED}}**

  Only support installing firmware signed with a trusted key.
  Do not set this to `false` on a production or trusted system.

**ShowDevicePrivate={{FU_DAEMON_CONFIG_DEFAULT_SHOW_DEVICE_PRIVATE}}**

  Show data such as device serial numbers which some users may consider private.

**AllowEmulation={{FU_DAEMON_CONFIG_DEFAULT_ALLOW_EMULATION}}**

  Allow capturing and loading device emulation by logging all USB transfers.
  Enabling this will greatly increase the amount of memory fwupd uses when upgrading devices.

**TrustedUids={{FU_DAEMON_CONFIG_DEFAULT_TRUSTED_UIDS}}**

  UIDs matching these values that call the D-Bus interface should marked as trusted.

**HostBkc={{FU_DAEMON_CONFIG_DEFAULT_HOST_BKC}}**

  A host best known configuration is used when using `fwupdmgr sync` which can downgrade firmware to factory versions or upgrade firmware to a supported config level. e.g. **vendor-factory-2021q1**

**EspLocation=**

  Override the location used for the EFI system partition (ESP) path.
  This is typically used if UDisks is not available, or was not able to automatically identify the location for any reason.

**Manufacturer=**

**ProductName=**

**ProductSku=**

**Family=**

**EnclosureKind=**

**BaseboardProduct=**

**BaseboardManufacturer=**

  Override values for SMBIOS or Device Tree data on the local system.
  These are only required when the SMBIOS or Device Tree data is invalid, missing, or to simulate running on another system.
  Empty values should be used to populate blank entries or add values to populate specific entries.

**TrustedReports={{FU_DAEMON_CONFIG_DEFAULT_TRUSTED_REPORTS}}**

  Vendor reports matching these expressions will have releases marked as `trusted-report`, e.g.

* `DistroId=chromeos`

* `DistroId=fedora&VendorId=19`

* `DistroId=fedora&VendorId=$OEM`

* `DistroId=fedora;DistroId=rhel&DistroVersion=9`

  NOTE: a `VendorId` of `$OEM` represents the OEM vendor ID of the vendor that owns the firmware,
  for example, where Lenovo QA has generated a signed report for a Lenovo laptop.

  There are also three os-release values available, `$ID`, `$VERSION_ID` and `$VARIANT_ID`, which
  allow expressions like:

* `DistroId=$ID`

* `DistroId=$ID,DistroVersion=$VERSION_ID`

UEFI_CAPSULE PARAMETERS
-----------------------

The `[uefi_capsule]` section can contain the following parameters.

**EnableGrubChainLoad={{FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_GRUB_CHAIN_LOAD}}**

  Configure GRUB to launch `fwupdx64.efi` instead of using other methods such as NVRAM or Capsule-On-Disk.

**DisableShimForSecureBoot={{FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_SHIM_FOR_SECURE_BOOT}}**

  The shim loader is required to chainload the fwupd EFI binary unless the `fwupd.efi` file has been self-signed manually.

**RequireESPFreeSpace={{FU_UEFI_CAPSULE_CONFIG_DEFAULT_REQUIRE_ESP_FREE_SPACE}}**

  Amount of free space required on the ESP, for example using `32` for 32Mb.
  By default this is dynamically set to at least twice the size of the payload.

**DisableCapsuleUpdateOnDisk={{FU_UEFI_CAPSULE_CONFIG_DEFAULT_DISABLE_CAPSULE_UPDATE_ON_DISK}}**

  Allow ignoring the CapsuleOnDisk support advertised by the firmware.

**EnableEfiDebugging={{FU_UEFI_CAPSULE_CONFIG_DEFAULT_ENABLE_EFI_DEBUGGING}}**

  Enable the low-level debugging of `fwupdx64.efi` to the `FWUPDATE_DEBUG_LOG` EFI variable.

  **NOTE:** enabling this option is going to fill up the NVRAM store much more quickly and
  should only be enabled when debugging an issue with the EFI binary.

  This value also has no affect when using Capsule-on-Disk as the EFI helper binary is
  not being used.

MSR PARAMETERS
--------------

The `[msr]` section can contain the following parameters.

**MinimumSmeKernelVersion={{FU_MSR_CONFIG_DEFAULT_MINIMUM_SME_KERNEL_VERSION}}**

  Minimum kernel version to allow probing for sme flag.

  This only needs to be modified by enterprise kernels that have cherry picked the feature into a
  kernel with an old version number.

REDFISH PARAMETERS
------------------

The `[redfish]` section can contain the following parameters.

**Uri=**

  The URI to the Redfish service in the format `scheme://ip:port` for instance `https://192.168.0.133:443`

**Username=**

  The username to use when connecting to the Redfish service.

**Password=**

  The password to use when connecting to the Redfish service.

**CACheck={{FU_REDFISH_CONFIG_DEFAULT_CA_CHECK}}**

  Whether to verify the server certificate or not. This is turned off by default.
  BMCs using self-signed certificates will not work unless the plugin does not verify it against the system CAs.

**IpmiDisableCreateUser={{FU_REDFISH_CONFIG_DEFAULT_IPMI_DISABLE_CREATE_USER}}**

  Do not use IPMI KCS to create an initial user account if no SMBIOS data.
  Setting this to **true** prevents creating user accounts on the BMC automatically.

**ManagerResetTimeout={{FU_REDFISH_CONFIG_DEFAULT_MANAGER_RESET_TIMEOUT}}**

  Amount of time in seconds to wait for a BMC restart.

THUNDERBOLT PARAMETERS
----------------------

The `[thunderbolt]` section can contain the following parameters.

**MinimumKernelVersion={{FU_THUNDERBOLT_CONFIG_DEFAULT_MINIMUM_KERNEL_VERSION}}**

  Minimum kernel version to allow use of this plugin.

  This only needs to be modified by enterprise kernels that have cherry picked the feature into a
  kernel with an old version number.

**DelayedActivation={{FU_THUNDERBOLT_CONFIG_DEFAULT_DELAYED_ACTIVATION}}**

  Forces delaying activation until shutdown/logout/reboot.

**RetimerOfflineMode={{FU_THUNDERBOLT_CONFIG_DEFAULT_RETIMER_OFFLINE_MODE}}**

  Uses offline mode interface to update retimers.

NOTES
-----

{{SYSCONFDIR}}/fwupd/fwupd.conf` may contain either hardcoded or autogenerated credentials and must only be
readable by the user that is running the fwupd process, which is typically `root`.

SEE ALSO
--------

`fwupdmgr(1)`
