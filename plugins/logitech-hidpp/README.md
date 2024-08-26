---
title: Plugin: Logitech HID++
---

## Introduction

This plugin can flash the firmware on:

* Logitech Unifying USB receivers, both the Nordic (U0007) device and the
   Texas Instruments (U0008) versions
* Logitech Bolt USB receivers
* Unifying peripherals through the Unifying receiver
* Peripherals through the Bolt receiver and directly through BLE

This plugin will not work with the different "Nano" USB receiver (U0010) as it does
not use the Unifying protocol.

Some bootloader protocol information was taken from the [Mousejack](https://www.mousejack.com/) project,
specifically logitech-usb-restore.py and unifying.py. Other documentation was
supplied by Logitech.

Additional constants were taken from the [https://pwr-Solaar.github.io/Solaar/](Solaar) project.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
a vendor-specific format that appears to be a subset of the Intel HEX format.

This plugin supports the following protocol IDs:

* `com.logitech.unifying`
* `com.logitech.unifyingsigned`

## GUID Generation

The Unifying receivers and peripherals use the standard USB
DeviceInstanceId values when in DFU mode:

* `USB\VID_046D&PID_AAAA`

When in runtime mode, the HID raw DeviceInstanceId values are used:

* `HIDRAW\VEN_046D&MOD_B33B405B0000`
* `HIDRAW\VEN_046D&MOD_B33B405B0000&ENT_05`
* `HIDRAW\VEN_046D&DEV_C52B`
* `HIDRAW\VEN_046D&DEV_C52B&ENT_05`

One additional legacy instance ID is added for peripherals:

* `UFY\VID_046D&PID_C52B`

The Bolt USB receiver and peripherals use HID raw DeviceInstanceId values
regardless of their mode. This might change once these devices are
handled by the Logitech Linux driver instead of by the generic hid
driver.

## Vendor ID Security

The vendor ID is set from the vendor ID, in this instance set to `USB:0x046D`
in bootloader and `HIDRAW:0x046D` in runtime mode.

## Update Behavior

Due to the variety of devices supported and the differences in how
they're enumerated, the update behavior is slightly different between
them.

In all cases, the devices have to be put in bootloader mode to run the
DFU process. While in bootloader mode, the user won't be able to use the
device. For receivers, that also means that while they're in bootloader
mode, the peripherals paired to them won't work during the update.

A Unifying receiver presents in runtime mode, but on detach re-enumerates with a
different USB PID in a bootloader mode. On attach the device again re-enumerates
back to the runtime mode.

For this reason the `REPLUG_MATCH_GUID` internal device flag is used so that
the bootloader and runtime modes are treated as the same device.

The Bolt receiver enumerates as a hidraw device both in runtime and
bootloader mode, but with different HIDRAW devIDs.

Peripherals paired to a receiver are enumerated as separate hidraw
devices but those device files can't be used for DFU. Instead, all the
DFU-related messages need to be piped through the hidraw device file of
the receiver. They are polled and queried by the receiver and listed as
its children. Note that this will likely change once the Logitech Linux
driver supports Bolt devices.

Bolt peripherals directly connected to the host through BLE are
enumerated as individual hidraw devices and can be upgraded through
their hidraw device files.

## Design Notes

When a USB receiver is detected in bootloader mode we detach the hidraw driver from
the kernel and use raw control transfers. This ensures that we don't accidentally
corrupt the uploading firmware. For application firmware we use hidraw which
means the hardware keeps working while probing, and also allows us to detect
paired devices.

### How the code is organized

Here's how the different devices are handled in the plugin:

* Unifying receiver in runtime mode: FuLogitechHidppRuntimeUnifying
    (fu-logitech-hidpp-runtime-unifying.c)
* Unifying receiver in bootloader mode:
  * Nordic chipset: FuLogitechHidppBootloaderNordic
    (fu-logitech-hidpp-bootloader-nordic.c)
  * TI chipset: FuLogitechHidppBootloaderTexas
    (fu-logitech-hidpp-bootloader-texas.c)
* Bolt receiver in runtime mode: FuLogitechHidppRuntimeBolt
    (fu-logitech-hidpp-runtime-bolt.c)
* Bolt receiver in bootloader mode and all peripherals:
    FuLogitechHidppDevice (fu-logitech-hidpp-device.c)

FuLogitechHidppDevice effectively handles all devices that use the
HID++2.0 protocol.

Every device contains two updatable entities, the main application FW
and the radio stack FW (SoftDevice). The latter will show up as a child
device of the actual device and is handled by FuLogitechHidppRadio
(fu-logitech-hidpp-radio.c), which simply defers to the parent device
for most operations.

### Quirk Use

Even though the same code handles multiple different devices, there are
some inherent differences in them that makes it necessary to handle some
exceptional behaviors sometimes.

This plugin uses the following plugin-specific quirks:

### `Flags=rebind-attach`

Some devices will have their device file unbound and re-bound after reset, so the device object
can't be simply re-probed using the same file descriptor.

Since: 1.7.0

### `Flags=force-receiver-id`

Used to differentiate the receiver device in FuLogitechHidppDevice, since the receiver has a
specific HID++ ID.

Since: 1.7.0

### `Flags=ble`

Differentiate devices in BLE mode. They require all the reports to be _long_.

Since: 1.7.0

### `Flags=is-signed`

Device requires signed firmware.

Since: 1.7.0

### `Flags=no-request-required`

No user-action is required for detach and attach.

Since: 1.7.0

### `Flags=add-radio`

The device should add a softdevice (index 0x5), typically a radio.

Since: 1.7.0

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `1.3.4`.
