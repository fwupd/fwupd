---
title: Plugin: Dell USB-C Dock
---

## Dell System

This plugin supports Dell K2 dock with no dependencies on Dell system.

## Components

Supported hardware devices:

* RTS5480 usb 3 gen 1 hub controller
* RTS5480 usb 3 gen 2 hub controller
* RTS5485 usb 3 gen 2 hub controller
* MST 8430 controller
* MST 9430 controller
* GR thunderbolt controller
* BR thunderbolt controller
* Embedded controller
* Ethernet controller
* Retimer/dpmux controller
* PD controller
* Remote management controller

## Device topology

When this plugin is used, devices present in other plugins may be shown in
the topology of this dock.  This is intentional as this plugin works together
with those plugins to manage the flashing of all components.

* synaptics_vmm9
* intel_usb4

## Firmware Format

The daemon will decompress the cabinet archive and extract several firmware
blobs with an unspecified binary file format.

This plugin supports the following protocol ID:

* `com.dell.k2`

## GUID Generation

These devices use several different generation schemes, e.g.

### Dock config independent devices

* Embedded Controller: `USB\VID_413C&PID_B06E&DOCKTYPE_07&DEVTYPE_00`
* RTS5480 usb 3 gen 1 hub: `USB\VID_413C&PID_B0A1&DOCKTYPE_07`
* RTS5480 usb 3 gen 2 hub: `USB\VID_413C&PID_B0A2&DOCKTYPE_07`
* RTS5485 usb 3 gen 2 hub: `USB\VID_413C&PID_B0A3&DOCKTYPE_07`
* Remote Management Controller: `EC\DOCKTYPE_07&DEVTYPE_09`
* MST 8430/9430: `USB\VID_413C&PID_B0A5`
* Ethernet: `EC\DOCKTYPE_07&DEVTYPE_07`

### DP ALT config

* Package: `EC\DOCKTYPE_07&DOCKSKU_01&DEVTYPE_PACKAGE`
* PD UP 5: `EC\DOCKTYPE_07&DOCKSKU_01&DEVTYPE_01&INST_00`
* PD UP 15: `EC\DOCKTYPE_07&DOCKSKU_01&DEVTYPE_01&INST_01`

### TBT4 config

* Package: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_PACKAGE`
* PD UP 5: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_01&INST_00`
* PD UP 15: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_01&INST_01`
* PD UP 17: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_01&INST_02`
* TBT4 Controller: `TBT-00d4b0a1`

### TBT5 config

* Package: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_PACKAGE`
* PD UP 5: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_01&INST_00`
* PD UP 15: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_01&INST_01`
* PD UP 17: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_01&INST_02`
* Retimer: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_06`
* Weltrend PD: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_10`
* TBT5 Controller: `TBT-00d4b0a2`

## Update Behavior

The `rtsusb` hub, `mst` and `ec` use `hid` over usb interface for device
communication while the `thunderbolt` controller use `usb`.  The `ethernet`,
`retimer/dpmux`, `package`, various `pd` and the `remote management` controller
will be updated through `ec` device over internal `i2c` interface.

As this dock is a composite device with inclusion of various components, the
update takes particular order to completion.

All updates will be staged on the device until the user manually disconnects
the dock's Type-C cable from the host, at which point they will take effect.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x413C`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `uod-off`

Disable update on disconnect feature, default unset.

Since 1.9.27

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Crag Wang: @CragW
