Dell Support
============

Introduction
------------

This allows installing Dell capsules that are not part of the ESRT table.

Build Requirements
------------------

For Dell support you will need libsmbios_c version 2.3.0 or later and
efivar.
* source:		http://linux.dell.com/cgi-bin/cgit.cgi/libsmbios.git/
* rpms:		https://apps.fedoraproject.org/packages/libsmbios
* debs (Debian):	http://tracker.debian.org/pkg/libsmbios
* debs (Ubuntu):	http://launchpad.net/ubuntu/+source/libsmbios

If you don't want or need this functionality you can use the
`--disable-dell` option.

# Devices powered by the Dell Plugin
The Dell plugin creates device nodes for PC's that have switchable TPMs as
well as the Type-C docks (WD15/TB16).

These device nodes can be flashed using UEFI capsule (and fwupdate) but don't
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
