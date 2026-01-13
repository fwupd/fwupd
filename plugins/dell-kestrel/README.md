---
title: Plugin: Dell USB-C Dock
---

## Dell System

This plugin supports Dell dock with no dependencies on Dell system.

## Components

Supported hardware devices:

* USB Hubs
* MST
* TBT
* EC
* LAN
* Retimer/dpmux
* PD
* RMM

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

* `com.dell.kestrel`

## GUID Generation

These devices use several different generation schemes, e.g.

### Dock config independent devices

* Embedded Controller: `USB\VID_413C&PID_B06E&DOCKTYPE_07&DEVTYPE_00`
* RTS0 USB 3 G1 Hub: `USB\VID_413C&PID_B0A1&DOCKTYPE_07`
* RTS0 USB 3 G2 hub: `USB\VID_413C&PID_B0A2&DOCKTYPE_07`
* RTS5 USB 3 G2 hub: `USB\VID_413C&PID_B0A3&DOCKTYPE_07`
* RMM: `USB\VID_413C&PID_B0A4`
* MST: `USB\VID_413C&PID_B0A5`
* LAN: `EC\DOCKTYPE_07&DEVTYPE_07`

### DP ALT config

* Package: `EC\DOCKTYPE_07&DOCKSKU_01&DEVTYPE_PACKAGE`
* PD UP 5: `EC\DOCKTYPE_07&DOCKSKU_01&DEVTYPE_01&INST_00`
* PD UP 15: `EC\DOCKTYPE_07&DOCKSKU_01&DEVTYPE_01&INST_01`

### T4 config

* Package: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_PACKAGE`
* PD UP 5: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_01&INST_00`
* PD UP 15: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_01&INST_01`
* PD UP 17: `EC\DOCKTYPE_07&DOCKSKU_02&DEVTYPE_01&INST_02`
* TBT Controller: `TBT-00d4b0a1`

### T5 config

* Package: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_PACKAGE`
* PD UP 5: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_01&INST_00`
* PD UP 15: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_01&INST_01`
* PD UP 17: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_01&INST_02`
* Retimer: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_06`
* WT PD: `EC\DOCKTYPE_07&DOCKSKU_03&DEVTYPE_10`
* TBT Controller: `TBT-00d4b0a2`

## Update Behavior

This dock contains multiple components and updates in a specific sequence.

Components don't reboot during firmware writing; updates are staged and managed
activation according to the plugin-defined process during the `composite_cleanup`
phase. The Power Button LED of the dock will flash three times to indicate the
update is completed.

There are two ways to trigger the staged firmware, and the update behavior can
be adjusted via the plugin configuration `UpdateOnDisconnect`.

fwupd versions with UOC approach are listed below, otherwise use UOD.

* 1.9.33
* 2.0.17
* 2.0.18
* 2.0.19

### Update on Connected (UOC)

When all the component updates are installed and staged to the dock, the dock
reboots the devices to activate the new version immediately. The connected
peripherals on the dock side will be disconnected from the host, and take a few
minutes to finish.

### Update on Disconnected (UOD)

Staged updates are activated when the dock's Type-C cable is disconnected from
the host, while the power cable must remain connected to support the update process.
This design ensures devices remain usable during firmware installation in the
user's OS runtime session.

## Plugin Configuration

This plugin supports the following parameters by modifying `dell_kestrel` section
of `/etc/fwupd/fwupd.conf`.

### UpdateOnDisconnect

The firmware updates are staged to the devices in the dell dock and activated
when the user manually unplugs the dock cable. Default: false.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x413C`
