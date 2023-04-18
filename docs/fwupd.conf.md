---
title: Configuration File Format
---

% fwupd.conf(5) @PACKAGE_VERSION@ | Configuration File Format

NAME
----

**fwupd.conf** — configuration file for the fwupd daemon.

SYNOPSIS
--------

The `@SYSCONFDIR@/fwupd/fwupd.conf` file is the main configuration file for the fwupd daemon.
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

**DisabledDevices=**[GUID];[GUID]

  Allow blocking specific devices by their GUID, using semicolons as delimiter.

**DisabledPlugins=**[PLUGIN_NAME];[PLUGIN_NAME]

  Allow blocking specific plugins by name.
  Use **fwupdmgr get-plugins** to get the list of plugins.

**ArchiveSizeMax=**[SIZE_IN_MB]

  Maximum archive size that can be loaded in Mb, with **0** for the default.

**IdleTimeout=**[IDLE_TIME_IN_SECONDS]

  Idle time in seconds to shut down the daemon.
  NOTE: some plugins might inhibit the auto-shutdown, for instance thunderbolt.

  A value of **0** specifies "never".

**VerboseDomains=**[DOMAIN]

  Comma separated list of domains to log in verbose mode.
  If unset, no domains are set to verbose.
  If set to "*", all domains are verbose, which is the same as running the daemon with **--verbose --verbose**.

**UpdateMotd=**[BOOL]

  Update the message of the day (MOTD) on device and metadata changes.

**EnumerateAllDevices=**[BOOL]

  For some plugins, enumerate only devices supported by metadata.

**ApprovedFirmware=**[HASH];[HASH];[HASH]

  A list of firmware checksums that has been approved by the site admin
  If unset, all firmware is approved.

**BlockedFirmware=**[HASH];[HASH];[HASH]

  Allow blocking specific devices by their cabinet checksum, either SHA-1 or SHA-256.

**UriSchemes=**

  Allowed URI schemes in the preference order; failed downloads from the first scheme will be retried with the next in order until no choices remain.
  If unset or no schemes are listed, the default will be: **file,https,http,ipfs**.

**IgnorePower=**[BOOL]

  Ignore power levels of devices when running updates.

**OnlyTrusted=**[BOOL]

  Only support installing firmware signed with a trusted key.
  Do not set this to `false` on a production or trusted system.

**ShowDevicePrivate=**[BOOL]

  Show data such as device serial numbers which some users may consider private.

**AllowEmulation=**[BOOL]

  Allow capturing and loading device emulation by logging all USB transfers.
  Enabling this will greatly increase the amount of memory fwupd uses when upgrading devices.

**TrustedUids=**[INTEGER_UIDS]

  UIDs matching these values that call the D-Bus interface should marked as trusted.

**HostBkc=**[STRING]

  A host best known configuration is used when using `fwupdmgr sync` which can downgrade firmware to factory versions or upgrade firmware to a supported config level. e.g. **vendor-factory-2021q1**

**EspLocation=**[PATH]

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

**TrustedReports=**[EXPRESSION];[EXPRESSION]

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

**EnableGrubChainLoad=**[BOOL]

  Configure GRUB to launch `fwupdx64.efi` instead of using other methods such as NVRAM or Capsule-On-Disk.

**DisableShimForSecureBoot=**[BOOL]

  The shim loader is required to chainload the fwupd EFI binary unless the `fwupd.efi` file has been self-signed manually.

**RequireESPFreeSpace=**[SIZE_IN_MB]

  Amount of free space required on the ESP, for example using `32` for 32Mb.
  By default this is dynamically set to at least twice the size of the payload.

**DisableCapsuleUpdateOnDisk=**[BOOL]

  Allow ignoring the CapsuleOnDisk support advertised by the firmware.

**EnableEfiDebugging=**[BOOL]

  Enable the low-level debugging of `fwupdx64.efi` to the `FWUPDATE_DEBUG_LOG` EFI variable.

  **NOTE:** enabling this option is going to fill up the NVRAM store much more quickly and
  should only be enabled when debugging an issue with the EFI binary.

  This value also has no affect when using Capsule-on-Disk as the EFI helper binary is
  not being used.

MSR PARAMETERS
--------------

The `[msr]` section can contain the following parameters.

**MinimumSmeKernelVersion=**[KERNEL_VERSION]

  Minimum kernel version to allow probing for sme flag.
  This only needs to be modified by enterprise kernels that have cherry picked the feature into a
  kernel with an old version number.

REDFISH PARAMETERS
------------------

The `[redfish]` section can contain the following parameters.

**Uri=**[URL]

  The URI to the Redfish service in the format `scheme://ip:port` for instance `https://192.168.0.133:443`

**Username=**[VALUE]

  The username to use when connecting to the Redfish service.

**Password=**[VALUE]

  The password to use when connecting to the Redfish service.

**CACheck=**[BOOL]

  Whether to verify the server certificate or not. This is turned off by default.
  BMCs using self-signed certificates will not work unless the plugin does not verify it against the system CAs.

**IpmiDisableCreateUser=**[BOOL]

  Do not use IPMI KCS to create an initial user account if no SMBIOS data.
  Setting this to **true** prevents creating user accounts on the BMC automatically.

**ManagerResetTimeout=**[TIMEOUT_IN_SECONDS]

  Amount of time in seconds to wait for a BMC restart.

THUNDERBOLT PARAMETERS
----------------------

The `[thunderbolt]` section can contain the following parameters.

**MinimumKernelVersion=4.13.0**

  Minimum kernel version to allow use of this plugin.
  This only needs to be modified by enterprise kernels that have cherry picked the feature into a
  kernel with an old version number.

**DelayedActivation=**[BOOL]

  Forces delaying activation until shutdown/logout/reboot.

**RetimerOfflineMode=**[BOOL]

  Uses offline mode interface to update retimers.

NOTES
-----

`@SYSCONFDIR@/fwupd/fwupd.conf` may contain either hardcoded or autogenerated credentials and must only be
readable by the user that is running the fwupd process, which is typically `root`.

SEE ALSO
--------

`fwupdmgr(1)`
