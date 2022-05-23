# SteelSeries

## Introduction

This plugin allows to update firmware on SteelSeries gamepads:

* Stratus Duo
* Stratus Duo USB wireless adapter
* Stratus+
* Aerox 3 Wireless
* Rival 3 Wireless

SteelSeries Rival 100 gaming mice support is limited by getting the correct
version number. These mice have updatable firmware but so far no updates are
available from the vendor.

This plugin supports the following protocol IDs:

* com.steelseries.fizz
* com.steelseries.gamepad
* com.steelseries.sonic

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_1038&PID_1702&REV_0001`
* `USB\VID_1038&PID_1702`
* `USB\VID_1038`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### Flags:is-receiver

The device is a USB receiver.

Since 1.8.1

## Update Behavior

### Gamepad

Gamepad and/or its wireless adapter must be connected to host via USB cable
to apply an update. The device is switched to bootloader mode to flash
updates, and is reset automatically to new firmware after flashing.

### Mice

The device is not upgradable and thus requires no vendor ID set.

### Wireless Mice

### Rival 3 Wireless

The mouse switch button underneath must be set to 2.4G, and its 2.4G USB
Wireless adapter must be connected to host.

### Aerox 3 Wireless

The mouse must be connected to host via the USB-A to USB-C cable.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x1038`

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
