---
title: Plugin: CFU - Component Firmware Update
---

## Introduction

CFU is a protocol from Microsoft to make it easy to install firmware on HID devices.

See <https://docs.microsoft.com/en-us/windows-hardware/drivers/cfu/cfu-specification> for more
details.

This plugin supports the following protocol ID:

* `com.microsoft.cfu`

## Implementation Notes

CFU has a pre-download phase that is used to send the firmware *offer* to the microcontroller, so the
device can check if the firmware is required and compatible.

CFU also requires devices to be able to transfer the entire new transfer mode in runtime mode.
The pre-download “offer” allows the device to check any sub-components attached (e.g. other devices
attached to the SoC) and forces it to do dependency resolution in case sub-components have to be
updated in a specific order.

Pushing the dependency resolution down to the device means the low-power device has to do all the
version comparisons and also know all the logic with regard to protocol incompatibilities.
The end-user could be in a position where the device firmware needs to be updated so that it “knows”
about the new protocol restrictions, which are needed to update the device and the things attached
in the right order in a subsequent update.
If the user always updates the device to the latest version, the factory-default running version
*might yet know* about the new restrictions.
It is therefore imperative that all previous versions are tested being updated *from*.

Something that we support in fwupd is being able to restrict the peripheral device firmware to a
specific SMBIOS CHID or a system firmware vendor, which lets vendors solve the *same hardware in
different chassis, with custom firmware* problem.
Using CFU in Microsoft Windows also means that the peripheral is unaware of the other devices in the
system, so for instance couldn’t only install a new firmware version for only new builds of Windows
for example.

A few other consideration for vendors is the doubling of flash storage required to do an runtime
transfer, the extra power budget of being woken up to process the *offer* and providing enough bulk
power to stay alive if *unplugged* during a A/B swap.
On most existing hardware the easiest way to implement CFU is an additional ARM micro-controller
to act as a CFU “bridge” for legacy silicon. The CFU “bridge” could also do signing and encryption.
CFU does not define any standard way to encrypt and sign firmware, or to detect if devices have any
firmware verification capabilities and so this too will need to be set per-device either in the
metadata or in the quirk file.

CFU also downloads in the runtime mode in the background, at a maximum of 52 bytes per HID request
and response.
This means even small updates will take a long time to complete due to the huge number of USB control
transfers required.
The specification also doesn't specify the HID reports to use, so it all needs to be hardcoded
per-device unless the exact same defaults are used as in `CFU/Tools/ComponentFirmwareUpdateStandAloneToolSample/protocolCfgExample.cfg`.

In fwupd these can be set as quirks in `cfu.quirk`.

The included `https://github.com/fwupd/fwupd/blob/main/contrib/cfu-inf-to-quirk.py` script may be
useful to convert an existing `.inf` file to fwupd `.quirk` format.

## Firmware Format

Due to the one-shot way fwupd deploys firmware, the daemon only deals with one “payload” per
update. The offer and payload currently have to be combined in an archive where they are
transferred to the device one after the other.

The files in the firmware archive therefore should have the extensions `.offer.bin` and `.payload.bin`.

## GUID Generation

These devices use standard USB DeviceInstanceId values, as well as two extra for the component ID
and the bank, e.g.

* `HIDRAW\VEN_17EF&DEV_7226&CID_01&BANK_1`
* `HIDRAW\VEN_17EF&DEV_7226&CID_01`
* `HIDRAW\VEN_17EF&DEV_7226`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### CfuVersionGetReport

The HID report usage to use when parsing the response of `GET_FIRMWARE_VERSION`.

This usually corresponds to the `VersionsFeatureValueCapabilityUsageRangeMinimum` value
set in the `.inf` file.

Since: 1.9.1

### CfuOfferSetReport

The HID report usage to use when sending the request for `FIRMWARE_UPDATE_OFFER`.

This usually corresponds to the `OfferOutputValueCapabilityUsageRangeMinimum` value
set in the `.inf` file.

Since: 1.9.1

### CfuOfferGetReport

The HID report usage to use when parsing the response of `FIRMWARE_UPDATE_OFFER`.

This usually corresponds to the `OfferInputValueCapabilityUsageRangeMinimum` value
set in the `.inf` file.

Since: 1.9.1

### CfuContentSetReport

The HID report usage to use when sending the request for `FIRMWARE_UPDATE_CONTENT`.

This usually corresponds to the `PayloadOutputValueCapabilityUsageRangeMinimum` value
set in the `.inf` file.

Since: 1.9.1

### CfuContentGetReport

The HID report usage to use when parsing the response of `FIRMWARE_UPDATE_CONTENT`.

This usually corresponds to the `PayloadInputValueCapabilityUsageRangeMinimum` value
set in the `.inf` file.

Since: 1.9.1

## Update Behavior

The device has to support runtime updates and does not have a detach-into-bootloader mode -- but
after the install has completed the device still has to reboot into the new firmware.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `HIDRAW:0x17EF`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.7.1`.
