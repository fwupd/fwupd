# Dell USB-C Dock

## Dell System

Unlike previous Dell USB-C devices, a Dell system is not needed for updating.

## Components

The device contains components the following directly updatable components:

* USB hubs
* MST controller
* Thunderbolt controller
* Embedded controller

This plugin is used to perform the update on the USB hubs as well as the Dell
Embedded controller.  The USB hubs are updated directly over a USB HID endpoint
while the embedded controller is updated using an I2C over HID interface.

The fwupd thunderbolt plugin is used for updating the Titan Ridge controller.

The MST controller is updated through either the DP Aux interface
(SynapticsMST plugin) or I2C over HID interface provided by this plugin.

## Device topology

When this plugin is used, devices present in other plugins may be shown in
the topology of this dock.  This is intentional as this plugin works together
with those plugins to manage the flashing of all components.

## Firmware Format

The daemon will decompress the cabinet archive and extract several firmware
blobs with an unspecified binary file format.

This plugin supports the following protocol ID:

* com.dell.dock
* com.synaptics.mst

## GUID Generation

These devices use several different generation schemes, e.g.

* USB Hub1: `USB\VID_413C&PID_B06F&hub`
* USB Hub2: `USB\VID_413C&PID_B06E&hub`
* Embedded Controller: `USB\VID_413C&PID_B06E&hub&embedded`
* Update Level: `USB\VID_413C&PID_B06E&hub&status`
* MST Hub: `MST-panamera-vmm5331-259`
* Thunderbolt Controller: `TBT-00d4b070`
* USB4 Controller: `TBT-00d4b071`

## Update Behavior

All devices will be updated the next time the usb-c plug from the dock is unplugged from the host.

### USB4 Controller

This device will be probed by `dell-dock` plugin over the USB interface, additionally will be probed by `thunderbolt` plugin if thunderbolt hardware is enabled at the host. The primary plugin has been chosen to `dell-dock` for broader supoprt, the device introduced by `thunderbolt` plugin will be default inhibited, in this case `Update Error` will be seen which is expected.

```shell
USB4 controller in Dell dock:
    Device ID:        ce501f4b2e03e819c525bb9354aa88c03db4f11e
    Summary:          USB4 controller
    Current version:  36.00
    Vendor:           Dell Inc. (THUNDERBOLT:0x00D4, TBT:0x00D4)
    Install Duration: 46 seconds
    Update Error:     firmware update inhibited by [dell_dock] plugin
    GUIDs:            4fb9d92e-2b96-51a7-9ed5-3db156dfcf12 ← THUNDERBOLT\VEN_00D4&DEV_B071
                      bd79ce60-525b-5f39-a3f6-c98c495039ff ← TBT-00d4b071
                      03f008d5-d06a-5d2e-89ca-61f12a8dbf73 ← TBT-00d4b071-controller0-3
    Device Flags:     • System requires external power source
                      • Device stages updates
                      • Updatable
                      • Signed Payload
```

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x413C`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### DellDockUnlockTarget

The EC argument needed for unlocking certain device usage.

Since: 1.1.3

### DellDockBlobMajorOffset

The offset of the major version number in a payload.

Since: 1.1.3

### DellDockBlobMinorOffset

The offset of the minor version number in a payload

Since: 1.1.3

### DellDockBlobBuildOffset

The offset of the build version number in a payload

Since: 1.1.3

### DellDockBlobVersionOffset

The offset of the ASCII representation of a version string in a payload.

Since: 1.1.3

### DellDockBoardMin

The minimum board revision required to safely operate the plugin.

Since: 1.1.3

### DellDockVersionLowest

The minimum component version required to safely operate the plugin.

Since: 1.1.3

### DellDockBoard*

The board description of a board revision.

Since: 1.1.3

### DellDockInstallDurationI2C

The duration of time required to install a payload via I2C.

Since: 1.1.3

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.
