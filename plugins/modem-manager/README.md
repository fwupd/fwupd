---
title: Plugin: ModemManager
---

## Introduction

This plugin adds support for devices managed by ModemManager.

## GUID Generation

These device use the ModemManager "Firmware Device IDs" as the GUID, e.g.

* `USB\VID_413C&PID_81D7&REV_0318&CARRIER_VODAFONE`
* `USB\VID_413C&PID_81D7&REV_0318`
* `USB\VID_413C&PID_81D7`
* `USB\VID_413C`
* `PCI\VID_105B&PID_E0AB&REV_0000&CARRIER_VODAFONE`
* `PCI\VID_105B&PID_E0AB&REV_0000`
* `PCI\VID_105B&PID_E0AB`
* `PCI\VID_105B`
* `PCI\VID_1EAC&PID_1001`
* `PCI\VID_1EAC&PID_1002`
* `PCI\VID_1EAC`

## Quirk Use

This plugin uses the following plugin-specific quirk:

### ModemManagerBranchAtCommand

AT command to execute to determine the firmware branch currently installed on the modem.

Since: 1.7.4

### ModemManagerFirehoseProgFile

Firehose program file to use during the switch to EDL (Emergency Download) mode.

Since: 1.8.10

## Vendor ID Security

The vendor ID is set from the USB or PCI vendor, for example `USB:0x413C` `PCI:0x105B`

## Update method: fastboot

If the device supports the 'fastboot' update method, it must also report which
AT command should be used to trigger the modem reboot into fastboot mode.

Once the device is in fastboot mode, the firmware upgrade process will happen
as defined e.g. in the 'flashfile.xml' file. Every file included in the CAB that
is not listed in the associated 'flashfile.xml' will be totally ignored during
the fastboot upgrade procedure.

Update Protocol: `com.google.fastboot`

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the fastboot and runtime modes are treated as the same device.

## Update method: qmi-pdc

If the device supports the 'qmi-pdc' update method, the contents of the CAB
file should include files named as 'mcfg.*.mbn' which will be treated as MCFG
configuration files to download into the device using the Persistent Device
Configuration QMI service.

If a device supports both 'fastboot' and 'qmi-pdc' methods, the fastboot
operation will always be run before the QMI operation, so that e.g. the full
partition where the MCFG files are stored can be wiped out before installing
the new ones.

Update protocol: `com.qualcomm.qmi_pdc`

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the fastboot and runtime modes are treated as the same device.

## Update method: mbim-qdu

If the device supports the 'mbim-qdu' update method, the contents of the CAB
file should include a package named as 'Firmware_*.7z' which is a compressed
ota.bin file that will be downloaded to the ota partition of the device.

Update protocol: `com.qualcomm.mbim_qdu`

## Update method: firehose

If the device supports the 'firehose' update method, it should have QCDM port
exposed and the contents of the CAB file should contain 'firehose-rawprogram.xml'.
The device is then switched to the emergency download mode (EDL) and flashed
with files described in 'firehose-rawprogram.xml'.

Update protocol: `com.qualcomm.firehose`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb` and `/dev/bus/pci`.
