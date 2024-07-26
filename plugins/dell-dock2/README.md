---
title: Plugin: Dell USB-C Dock
---

## Dell System

This plugin supports Dell K2 dock with no dependencies on Dell system.

## Components

The device contains components the following directly updatable components:

* USB hubs
* MST controller
* Thunderbolt controller
* Embedded controller
* Ethernet controller
* Retimer/Dpmux controller
* PD controllers
* Remote management controller

The USB hubs and Embedded controller will be updated through USB HID directly
while the PD controllers and the retimer will be routed through Embedded
Controller.

The MST uses `synaptics_vmm9` protocol and the thunderbolt controllers use
`intel_usb4` protocol, both on usb interface.

## Device topology

When this plugin is used, devices present in other plugins may be shown in
the topology of this dock.  This is intentional as this plugin works together
with those plugins to manage the flashing of all components.

As this dock is a composite device with inclusion of various components, the
update takes particular order to completion.

## Firmware Format

The daemon will decompress the cabinet archive and extract several firmware
blobs with an unspecified binary file format.

This plugin supports the following protocol ID:

* `com.dell.dock2`

## GUID Generation

These devices use several different generation schemes, e.g.

* USB Hub1: `USB\VID_413C&PID_B0A1&k2_hub`
* USB Hub2: `USB\VID_413C&PID_B0A2&k2_hub`
* USB Hub3: `USB\VID_413C&PID_B0A3&k2_hub`
* Embedded Controller: `USB\VID_413C&PID_B06E&hub&k2_embedded`
* Retimer (Config 3): `DPMUX\DOCKTYPE_07&DOCKSKU_03`
* Package Level (Config 1): `USB\VID_413C&PID_B06E&hub&k2_sku1_pkg`
* Package Level (Config 2): `USB\VID_413C&PID_B06E&hub&k2_sku2_pkg`
* Package Level (Config 3): `USB\VID_413C&PID_B06E&hub&k2_sku3_pkg`
* MST Hub: `MST-carrera-vmm9430-260`
* PD UP  5 (Config 1): `PD\DOCKTYPE_07&DOCKSKU_01&INSTANCE_00`
* PD UP 15 (Config 1): `PD\DOCKTYPE_07&DOCKSKU_01&INSTANCE_01`
* PD UP  5 (Config 2): `PD\DOCKTYPE_07&DOCKSKU_02&INSTANCE_00`
* PD UP 15 (Config 2): `PD\DOCKTYPE_07&DOCKSKU_02&INSTANCE_01`
* PD UP 17 (Config 2): `PD\DOCKTYPE_07&DOCKSKU_02&INSTANCE_02`
* PD UP  5 (Config 3): `PD\DOCKTYPE_07&DOCKSKU_03&INSTANCE_00`
* PD UP 15 (Config 3): `PD\DOCKTYPE_07&DOCKSKU_03&INSTANCE_01`
* PD UP 17 (Config 3): `PD\DOCKTYPE_07&DOCKSKU_03&INSTANCE_02`
* Weltrend PD: `WTPD\DOCKTYPE_07&DOCKSKU_03`
* USB4 Controller: `TBT-00d4b073`
* USB4v2 Controller: `TBT-00d4b072`

## Update Behavior

All devices will be updated the next time the usb-c plug from the dock is unplugged from the host.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x413C`

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Crag Wang: @CragW
* Mario Limonciello: @superm1
