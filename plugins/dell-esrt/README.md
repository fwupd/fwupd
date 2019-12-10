Dell ESRT Support
=================

Introduction
------------

This allows enabling the BIOS setup option for UEFI capsule updates without
manually going into BIOS setup.

GUID Generation
---------------

These device uses a hardcoded GUID of `2d47f29b-83a2-4f31-a2e8-63474f4d4c2e`.

Vendor ID Security
------------------

The vendor ID is hardcoded to `PCI:0x1028`.

Build Requirements
------------------

For Dell support you will need libsmbios_c version 2.4.0 or later.

* source: https://github.com/dell/libsmbios
* binaries: https://github.com/dell/libsmbios/releases

If you don't want or need this functionality you can use the
`-Dplugin_dell=false` option.

# Devices powered by the Dell Plugin
The Dell ESRT plugin allows the user to enable the UpdateCapsule functionality
at runtime. A reboot will be required and the BIOS administrator password
must not be set.

Machines that offer this functionality will display an extra device in
```# fwupdmgr get-devices``` output.

Example:
```
UEFI dummy device
  Guid:                 2d47f29b-83a2-4f31-a2e8-63474f4d4c2e
  Plugin:               dell-esrt
  Flags:                internal|updatable|locked
  Version:              0
  Created:              2018-06-25
```
