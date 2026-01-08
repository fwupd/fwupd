# Plugin: Lenovo Accessory

## Introduction

The Lenovo Accessory plugin supports the standardized firmware update protocol for Lenovo peripherals.
This protocol is MCU-agnostic and supports both HID and BLE transports.

## Protocol Support

This plugin enables firmware management across different connection types:

* **USB HID**: Communicates via Get/Set Feature reports on the `hidraw` subsystem.
* **Bluetooth LE (BLE)**: Uses a custom service with dedicated Write/Read characteristics.

## Supported Devices

The plugin identifies devices using specific hardware IDs:

### HID Devices (Single-bank)

* **Normal Mode**:
  * `HIDRAW\VEN_17EF&DEV_629D`
  * `HIDRAW\VEN_17EF&DEV_6201`
* **Bootloader Mode**:
  * `HIDRAW\VEN_17EF&DEV_6194` (Common bootloader PID)

### BLE Devices (Dual-bank)

* `BLUETOOTH\VID_17EF&PID_61FE`
* `BLUETOOTH\VID_17EF&PID_629A`

## Update Behavior

The update strategy differs based on the device's memory architecture:

### HID (Single-bank Update)

HID devices utilize a **single-bank** design. For these devices:

1. The device must reboot into a dedicated **bootloader mode** (`DEV_6194`) to receive firmware.
2. The plugin uses `CounterpartGuid` logic to map the runtime device to its bootloader interface.
3. A final restart is performed to jump back to the new application code.

### BLE (Dual-bank / In-place Update)

BLE devices utilize a **dual-bank** design. For these devices:

1. **No Bootloader required**: Firmware is written to the secondary bank while the device is in runtime mode.
2. The device remains functional during the transfer, minimizing user downtime.
3. A reboot is only triggered at the end to swap the active banks and apply the update.
