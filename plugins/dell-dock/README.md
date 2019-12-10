Dell USB-C Dock
=========

### Dell System
Unlike previous Dell USB-C devices, a Dell system is not needed for updating.

### Components
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

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract several firmware
blobs with an unspecified binary file format.

This plugin supports the following protocol ID:

 * com.dell.dock
 * com.synaptics.mst

GUID Generation
---------------

These devices use several different generation schemes, e.g.

 * USB Hub1: `USB\VID_413C&PID_B06F&hub`
 * USB Hub2: `USB\VID_413C&PID_B06E&hub`
 * Embedded Controller: `USB\VID_413C&PID_B06E&hub&embedded`
 * Update Level: `USB\VID_413C&PID_B06E&hub&status`
 * MST Hub: `MST-panamera-vmm5331-259`
 * Thunderbolt Controller: `TBT-00d4b070`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, in this instance set to `USB:0x413C`

Custom flag use:
----------------
This plugin uses the following plugin-specific custom flags:

* `skip-restart`: Don't run the reset or reboot procedure of the component

Quirk use
---------
This plugin uses the following plugin-specific quirks:

| Quirk                        | Description                                                             | Minimum fwupd version |
|------------------------------|-------------------------------------------------------------------------|-----------------------|
| `DellDockUnlockTarget`       | The EC argument needed for unlocking certain device usage.              | 1.1.3                 |
| `DellDockBlobMajorOffset`    | The offset of the major version number in a payload                     | 1.1.3                 |
| `DellDockBlobMinorOffset`    | The offset of the minor version number in a payload                     | 1.1.3                 |
| `DellDockBlobBuildOffset`    | The offset of the build version number in a payload                     | 1.1.3                 |
| `DellDockBlobVersionOffset`  | The offset of the ASCII representation of a version string in a payload | 1.1.3                 |
| `DellDockBoardMin`           | The minimum board revision required to safely operate the plugin        | 1.1.3                 |
| `DellDockVersionLowest`      | The minimum component version required to safely operate the plugin     | 1.1.3                 |
| `DellDockBoard*`             | The board description of a board revision                               | 1.1.3                 |
| `DellDockInstallDurationI2C` | The duration of time required to install a payload via I2C.             | 1.1.3                 |
