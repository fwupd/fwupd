Dell USB-C Dock
=========

### Dell System
Unlike previous Dell USB-C devices, a Dell system is not needed for updating.

GUID Generation
---------------

These devices use several different generation schemes, e.g.

 * USB Hub1: `USB\VID_413C&PID_B06F&hub`
 * USB Hub2: `USB\VID_413C&PID_B06E&hub`
 * Embedded Controller: `USB\VID_413C&PID_B06E&hub&embedded`
 * Update Level: `USB\VID_413C&PID_B06E&hub&status`
 * MST Hub: `MST-panamera-vmm5331-259`
 * Thunderbolt Controller: `TBT-00d4b070`

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
