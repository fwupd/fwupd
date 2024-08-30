## Introduction

The plugin used for updating firmware on HP Fleetwood and Hendrix docking stations.

This plugin supports the following protocol ID:

* `com.microsoft.cfu`

## Implementation Notes

In fwupd these can be set as quirks in `hpi-cfu.quirk`.

## Firmware Format

The daemon only deals with one “payload” per update. The offer and payload currently have to
be combined in an archive where they are transferred to the device one after the other.

The files in the firmware archive therefore should have the extensions `.offer.bin` and `.payload.bin`
as a zip folder.

## GUID Generation

These devices use the standard USB DeviceInstanceId values as well as one extra for
the Revision number, e.g.

* `USB\VID_03F0&PID_0BAF`
* `USB\VID_03F0&PID_0BAF&REV_0001`

## Update Behavior

The device has to support runtime updates and does not have a detach-into-bootloader mode -- but
after the install has completed the device still has to reboot into the new firmware.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x03F0`

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Pena Christian <christian.a.pena@hp.com>
