---
title: Plugin: Lenovo Accessory
---

## Introduction

This plugin supports the standardized firmware update protocol used by Lenovo
peripherals and their wireless pairing receivers. The protocol is MCU-agnostic
and is carried over two transports:

* **USB HID**: command frames are exchanged as Get/Set Feature reports issued as
  control transfers on the receiver's vendor HID interface (interface 3). This
  does not rely on a `hidraw` kernel node, so it also works on receivers whose
  command interface is not bound by `usbhid`.
* **Bluetooth LE**: command frames are exchanged over a vendor-specific GATT
  service with dedicated write and read characteristics.

A single dongle may also expose the wireless peripherals paired to it as child
devices, which are updated by relaying commands through the dongle. The dongle
additionally pushes asynchronous notifications (e.g. a peripheral coming back
online after a reboot) as interrupt-IN input reports on interface 2.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unsigned, vendor-specific raw binary format. The blob is written to the
device as-is (file type `BIN`) and its CRC-32 is computed by the host and sent
to the device before the transfer begins.

This plugin supports the following protocol ID:

* `com.lenovo.accessory`

## GUID Generation

HID devices use the standard `USB` DeviceInstanceId values, e.g.

* `USB\VID_17EF&PID_629D` (runtime receiver)
* `USB\VID_17EF&PID_6194` (HID bootloader)

BLE devices and paired peripherals use the standard `BLUETOOTH` values, e.g.

* `BLUETOOTH\VID_17EF&PID_61FE`

Paired peripherals enumerated behind a dongle additionally get a `logical_id`
of `slot<slot>` (e.g. `slot1`) to disambiguate multiple peripherals on the same
receiver.

Single-bank receivers reboot into a dedicated bootloader mode that enumerates
with a different USB PID. To keep these treated as one physical device, the
runtime device declares its bootloader counterpart through the `CounterpartGuid`
quirk key, and the bootloader dynamically re-creates the runtime GUID at setup.
The `REPLUG_MATCH_GUID` internal flag lets fwupd match the two across the replug.

## Update Behavior

The update strategy depends on the device's memory architecture.

### Single-bank (HID receivers)

Single-bank devices cannot be written in place. The receiver is rebooted into a
dedicated bootloader mode to receive the firmware, then rebooted again to run
the new application:

1. The runtime device is detached by switching it into DFU mode; it
   re-enumerates as the bootloader (`USB\VID_17EF&PID_6194`).
2. The bootloader writes the whole image, then exits to jump back to the
   application.

Because the device is non-functional in bootloader mode (and so are any
peripherals paired to the receiver), it is unusable for the duration of the
update. The bootloader reports its version as `0.0.0` so that any firmware is
considered an upgrade, ensuring a device stuck in bootloader mode is always
recoverable.

### Dual-bank (BLE devices, HID dual receivers, paired peripherals)

Dual-bank devices are updated in place: firmware is written to the inactive bank
while the device keeps running (`USABLE_DURING_UPDATE`), and the host verifies
the written image by reading back the device CRC before the device reboots to
swap the active bank (`DUAL_IMAGE`).

## Vendor ID Security

The vendor ID is set from the transport vendor, in this instance set to
`USB:0x17EF` for HID devices and `BLUETOOTH:0x17EF` for BLE devices.

## Quirk Use

This plugin uses the following standard quirk keys:

### `CounterpartGuid`

Set on single-bank runtime receivers to declare the GUID of their bootloader
mode, so the runtime and bootloader enumerations are treated as one device.

### `Flags=no-generic-version`

Set on BLE peripherals to suppress the generic version, as the version is read
from the device over the vendor protocol.

The authoritative and complete device list is `lenovo-accessory.quirk`.

## External Interface Access

For HID devices this plugin requires read and write access to the receiver's USB
interfaces: the vendor HID command interface (interface 3) for control-transfer
Feature reports, and the notification interface (interface 2) for interrupt-IN
input reports. For BLE devices it requires read and write access to the vendor
GATT characteristics through BlueZ.

## Version Considerations

This plugin has been available since fwupd version `2.1.5`.
