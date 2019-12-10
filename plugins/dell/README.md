Dell Support
============

Introduction
------------

This allows installing Dell capsules that are not part of the ESRT table.

GUID Generation
---------------

These devices uses custom GUIDs for Dell-specific hardware.

 * Thunderbolt devices: `TBT-0x00d4u$(system-id)`
 * TPM devices `$(system-id)-$(mode)`, where `mode` is either `2.0` or `1.2`

In both cases the `system-id` is derived from the SMBIOS Product SKU property.

TPM GUIDs are also built using the TSS properties
`TPM2_PT_FAMILY_INDICATOR`, `TPM2_PT_MANUFACTURER`, and `TPM2_PT_VENDOR_STRING_*`
These are built hierarchically with more parts for each GUID:
 * `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1`
 * `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1$VENDOR_STRING_2`
 * `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1$VENDOR_STRING_2$VENDOR_STRING_3`
 * `DELL-TPM-$FAMILY-$MANUFACTURER-$VENDOR_STRING_1$VENDOR_STRING_2$VENDOR_STRING_3$VENDOR_STRING_4`

If there are non-ASCII values in any vendor string or any vendor is missing that octet will be skipped.

Example resultant GUIDs from a real system containing a TPM from Nuvoton:
```
  Guid:                 7d65b10b-bb24-552d-ade5-590b3b278188 <- DELL-TPM-2.0-NTC-NPCT
  Guid:                 6f5ddd3a-8339-5b2a-b9a6-cf3b92f6c86d <- DELL-TPM-2.0-NTC-NPCT75x
  Guid:                 fe462d4a-e48f-5069-9172-47330fc5e838 <- DELL-TPM-2.0-NTC-NPCT75xrls
```

Vendor ID Security
------------------

The vendor ID is hardcoded to `TPM:DELL`.

Build Requirements
------------------

For Dell support you will need libsmbios_c version 2.4.0 or later.

* source: https://github.com/dell/libsmbios
* binaries: https://github.com/dell/libsmbios/releases

If you don't want or need this functionality you can use the
`-Dplugin_dell=false` option.

# Devices powered by the Dell Plugin
The Dell plugin creates device nodes for PC's that have switchable TPMs as
well as the Type-C docks (WD15/TB16).

These device nodes can be flashed using UEFI capsule but don't
use the ESRT table to communicate device status or version information.

This is intentional behavior because more complicated decisions need to be made
on the OS side to determine if the devices should be offered to flash.

## Switchable TPM Devices
Machines with switchable TPMs can operate in both TPM 1.2 and TPM 2.0 modes.
Switching modes will require flashing an alternative firmware and clearing the
contents of the TPM.

Machines that offer this functionality will display two devices in
```# fwupdmgr get-devices``` output.

Example (from a *Precision 5510*):
```
Precision 5510 TPM 1.2
  Guid:                 b2088ba1-51ae-514e-8f0a-64756c6e4ffc
  DeviceID:             DELL-b2088ba1-51ae-514e-8f0a-64756c6e4ffclu
  Plugin:               dell
  Flags:                internal|allow-offline|require-ac
  Version:              5.81.0.0
  Created:              2016-07-19

Precision 5510 TPM 2.0
  Guid:                 475d9bbd-1b7a-554e-8ca7-54985174a962
  DeviceID:             DELL-475d9bbd-1b7a-554e-8ca7-54985174a962lu
  Plugin:               dell
  Flags:                internal|require-ac|locked
  Created:              2016-07-19
```

In this example, the TPM is currently operating in **TPM 1.2 mode**.  Any
firmware updates posted to *LVFS* for TPM 1.2 mode will be applied.

### Switching TPM Modes
In order to be offered to switch the TPM to **TPM 2.0 mode**, the virtual device
representing the *TPM 2.0 mode* will need to be unlocked.

```# fwupdmgr unlock DELL-475d9bbd-1b7a-554e-8ca7-54985174a962lu```

If the TPM is currently *owned*, an error will be displayed such as this one:

	ERROR: Precision 5510 TPM 1.2 is currently OWNED. Ownership must be removed to switch modes.

TPM Ownership can be cleared from within the BIOS setup menus.

If the unlock process was successful, then the devices will be modified:
```
Precision 5510 TPM 1.2
  Guid:                 b2088ba1-51ae-514e-8f0a-64756c6e4ffc
  DeviceID:             DELL-b2088ba1-51ae-514e-8f0a-64756c6e4ffclu
  Plugin:               dell
  Flags:                internal|require-ac
  Version:              5.81.0.0
  Created:              2016-07-19

Precision 5510 TPM 2.0
  Guid:                 475d9bbd-1b7a-554e-8ca7-54985174a962
  DeviceID:             DELL-475d9bbd-1b7a-554e-8ca7-54985174a962lu
  Plugin:               dell
  Flags:                internal|allow-offline|require-ac
  Version:              0.0.0.0
  Created:              2016-07-19
  Modified:             2016-07-19
```

Now the firmware for TPM 2.0 mode can be pulled down from LVFS and flashed:

```# fwupdmgr update```

Upon the next reboot, the new TPM firmware will be flashed.  If the firmware is
*not offered from LVFS*, then switching modes may not work on this machine.

After updating the output from ```# fwupdmgr get-devices```  will reflect the
new mode.

```
Precision 5510 TPM 2.0
  Guid:                 475d9bbd-1b7a-554e-8ca7-54985174a962
  DeviceID:             DELL-475d9bbd-1b7a-554e-8ca7-54985174a962lu
  Plugin:               dell
  Flags:                internal|allow-offline|require-ac
  Version:              1.3.0.1
  Created:              2016-07-20

Precision 5510 TPM 1.2
  Guid:                 b2088ba1-51ae-514e-8f0a-64756c6e4ffc
  DeviceID:             DELL-b2088ba1-51ae-514e-8f0a-64756c6e4ffclu
  Plugin:               dell
  Flags:                internal|require-ac|locked
  Created:              2016-07-20
```

Keep in mind that **TPM 1.2** and **TPM 2.0** will require different userspace
tools.

## Dock Devices
The *TB16* and *WD15* have a variety of updatable components.  Each component
will create a virtual device in ```# fwupdmgr get-devices```

For example the WD15 will display these components:
```
Dell WD15 Port Controller 1
  Guid:                 8ba2b709-6f97-47fc-b7e7-6a87b578fe25
  DeviceID:             DELL-8ba2b709-6f97-47fc-b7e7-6a87b578fe25lu
  Plugin:               dell
  Flags:                allow-offline|require-ac
  Version:              0.1.1.8
  Created:              2016-07-19

Dell WD15
  Guid:                 e7ca1f36-bf73-4574-afe6-a4ccacabf479
  DeviceID:             DELL-e7ca1f36-bf73-4574-afe6-a4ccacabf479lu
  Plugin:               dell
  Flags:                allow-offline|require-ac
  Version:              0.0.0.67
  Created:              2016-07-19
```

Components that can be updated via UEFI capsule will have the
```allow-offline``` moniker applied.

These updates can be performed the standard method of using:
```# fwupdmgr update```

Some components are updatable via other plugins in fwupd such as multi stream
transport hub (MST) and thunderbolt NVM.
