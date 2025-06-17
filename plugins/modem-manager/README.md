---
title: Plugin: ModemManager
---

## Introduction

This plugin adds support for devices managed by ModemManager.

## GUID Generation

These device use the ModemManager "Firmware Device IDs" as the GUID, e.g.

* `USB\VID_413C&PID_81D7&REV_0318&CARRIER_VODAFONE` (only if not using `Flags=use-branch`)
* `USB\VID_413C&PID_81D7&REV_0318`
* `USB\VID_413C&PID_81D7`
* `PCI\VID_105B&PID_E0AB&REV_0000&CARRIER_VODAFONE` (only if not using `Flags=use-branch`)
* `PCI\VID_105B&PID_E0AB&REV_0000`
* `PCI\VID_105B&PID_E0AB`

## Quirk Use

This plugin uses the following plugin-specific quirk:

### ModemManagerBranchAtCommand

AT command to execute to determine the firmware branch currently installed on the modem.

Since: 1.7.4

### ModemManagerFirehoseProgFile

Firehose program file to use during the QCDM switch to EDL (Emergency Download) mode.

Since: 1.8.10

### `Flags=use-branch`

Use the carrier (e.g. `VODAFONE`) as the device branch name so that `fwupdmgr sync` can downgrade
the firmware as required.

This is now the recommended mode for all modem devices with a carrier-specific firmware image,
although it requires that the firmware branch is also set in the firmware metadata.

Since: 1.9.8

### `Flags=detach-at-fastboot-has-no-response`

If no AT response is expected when entering fastboot mode.

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

## Update method: cinterion-fdl

If the device supports the 'cinterion-fdl' update method, it should have an AT-port
exposed. The device is then switched to Firmware Download Modem (FDL) and flashed
with the content of the firmware file. After an update, the device will not replug
until an ignition is sent, or the device is rebooted.

Update protocol: `com.cinterion.fdl`

## Update method: dfota

If the device supports the 'dfota' update method, it should have an AT-port
exposed. The device is then switched to data mode and downloads the
firmware to the ota partition via the AT-port. DFOTA updates require a specific
firmware version to be installed on the device, since the update only contains
a diff between the installed and target version.

Update protocol: `com.quectel.dfota`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb` and `/dev/bus/pci`.

## Version Considerations

This plugin has been available since fwupd version `1.2.6`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Aleksander Morgado: @aleksander0m
